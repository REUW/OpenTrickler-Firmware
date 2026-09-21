// Real-time runtime for the OpenTrickler simulator.
//
// Provides the FreeRTOS primitives on top of real OS threads, and runs the
// physical plant continuously in the background so that the charge state
// machine and the HTTP server both observe a pan that is genuinely filling
// in real time.

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
}

#include "sim_plant.h"

extern "C" {
sim_plant_config_t g_plant_cfg;
sim_plant_state_t g_plant_state;
}

// Guards every access to g_plant_state. The plant ticker writes it, the
// charge thread reads it through the scale, and the HTTP thread reads it for
// the live status display.
static std::recursive_mutex g_plant_mutex;

std::recursive_mutex &sim_plant_mutex() { return g_plant_mutex; }

static std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();
static std::atomic<bool> g_running{true};
static std::thread g_ticker;

extern "C" TickType_t xTaskGetTickCount(void) {
    auto now = std::chrono::steady_clock::now();
    return (TickType_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start).count();
}

extern "C" TickType_t sim_now_ticks(void) { return xTaskGetTickCount(); }

extern "C" void vTaskDelay(TickType_t ticks) {
    if (ticks == 0) {
        std::this_thread::yield();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

extern "C" void sim_advance_ticks(TickType_t ticks) { vTaskDelay(ticks); }

extern "C" void vTaskDelayUntil(TickType_t *previous, TickType_t increment) {
    TickType_t now = xTaskGetTickCount();
    TickType_t target = (previous ? *previous : now) + increment;
    if (target > now) {
        vTaskDelay(target - now);
    }
    if (previous) {
        *previous = target;
    }
}

// ---------------------------------------------------------------------------
// Plant ticker
// ---------------------------------------------------------------------------
// Integrates the physics on a fixed 5 ms cadence in its own thread. This is
// what makes powder keep flowing while the charge loop is between scale
// readings, exactly as on real hardware.
static void plant_ticker_thread() {
    const auto period = std::chrono::milliseconds(5);
    auto next = std::chrono::steady_clock::now();
    while (g_running.load()) {
        next += period;
        {
            std::lock_guard<std::recursive_mutex> lock(g_plant_mutex);
            sim_plant_step(&g_plant_cfg, &g_plant_state, 5);
        }
        std::this_thread::sleep_until(next);
    }
}

extern "C" void sim_rt_start(void) {
    g_start = std::chrono::steady_clock::now();
    g_running.store(true);
    g_ticker = std::thread(plant_ticker_thread);
}

extern "C" void sim_rt_stop(void) {
    g_running.store(false);
    if (g_ticker.joinable()) {
        g_ticker.join();
    }
}

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------
struct SimTask {
    std::thread thread;
    std::atomic<bool> suspended{false};
};

static std::vector<SimTask *> g_tasks;
static std::mutex g_tasks_mutex;

extern "C" BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint16_t stack,
                                  void *arg, UBaseType_t prio, TaskHandle_t *out) {
    (void)name; (void)stack; (void)prio;
    SimTask *t = new SimTask();
    {
        std::lock_guard<std::mutex> lock(g_tasks_mutex);
        g_tasks.push_back(t);
    }
    t->thread = std::thread([fn, arg, t]() {
        // Firmware tasks are written as infinite loops; they are detached and
        // simply run for the lifetime of the process.
        fn(arg);
    });
    t->thread.detach();
    if (out) {
        *out = (TaskHandle_t)t;
    }
    return pdPASS;
}

extern "C" void vTaskDelete(TaskHandle_t t) { (void)t; }

extern "C" void vTaskSuspend(TaskHandle_t t) {
    if (t) ((SimTask *)t)->suspended.store(true);
}

extern "C" void vTaskResume(TaskHandle_t t) {
    if (t) ((SimTask *)t)->suspended.store(false);
}

extern "C" BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
extern "C" UBaseType_t uxTaskPriorityGet(TaskHandle_t t) { (void)t; return 2; }
extern "C" void vTaskPrioritySet(TaskHandle_t t, UBaseType_t p) { (void)t; (void)p; }
extern "C" const char *pcTaskGetName(TaskHandle_t t) { (void)t; return "sim"; }

// ---------------------------------------------------------------------------
// Semaphores / mutexes
// ---------------------------------------------------------------------------
extern "C" SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)new std::recursive_mutex();
}
extern "C" SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (SemaphoreHandle_t)new std::recursive_mutex();
}
extern "C" SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    return (SemaphoreHandle_t)new std::recursive_mutex();
}

extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
    if (!s) return pdFALSE;
    auto *m = (std::recursive_mutex *)s;
    if (t == portMAX_DELAY) {
        m->lock();
        return pdTRUE;
    }
    // Poll rather than block forever, matching the firmware's timeout usage.
    TickType_t waited = 0;
    while (!m->try_lock()) {
        if (waited >= t) return pdFALSE;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waited++;
    }
    return pdTRUE;
}

extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    if (!s) return pdFALSE;
    ((std::recursive_mutex *)s)->unlock();
    return pdTRUE;
}

extern "C" BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) {
    return xSemaphoreTake(s, t);
}
extern "C" BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) {
    return xSemaphoreGive(s);
}
extern "C" void vSemaphoreDelete(SemaphoreHandle_t s) {
    delete (std::recursive_mutex *)s;
}
