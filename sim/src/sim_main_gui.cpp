// OpenTrickler simulator - real-time / web GUI mode.
//
// Boots the firmware's own subsystems, starts the physical plant running in
// real time, publishes the real REST endpoints over a host HTTP server, and
// runs the charge state machine in its own thread. Point a browser at
// http://localhost:8080 and you get the actual web portal talking to the
// actual firmware control code.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

extern "C" {
#include <FreeRTOS.h>
#include <task.h>
#include "charge_mode.h"
#include "profile.h"
#include "ai_tuning.h"
#include "motors.h"
#include "scale.h"
#include "http_rest.h"
#include "rest_ai_tuning.h"
}

#include "sim_plant.h"
#include "sim_http.h"
#include "sim_harness.h"

extern "C" {
extern sim_plant_config_t g_plant_cfg;
extern sim_plant_state_t g_plant_state;
extern charge_mode_config_t charge_mode_config;
void sim_rt_start(void);
void sim_rt_stop(void);
uint8_t charge_mode_menu(bool skip_user_input);
int g_sim_verbosity = 0;
}

// The batch harness used this for its motor trace; in GUI mode the web portal
// shows live state instead, so it just feeds the optional console trace.
extern "C" void sim_harness_on_motor_command(motor_select_t motor, float rps) {
    if (g_sim_verbosity < 2) {
        return;
    }
    printf("[%8u ms] %-6s -> %.3f rps   pan %.3f gn\n",
           (unsigned)xTaskGetTickCount(),
           motor == SELECT_COARSE_TRICKLER_MOTOR ? "coarse" : "fine",
           rps, g_plant_state.true_weight_gn);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Simulator-only REST endpoints
// ---------------------------------------------------------------------------
// The device detects cup removal and return by watching the weight drop and
// come back. There is no physical cup here, so expose it explicitly.
static char sim_json_buffer[512];

static bool sim_finalize(struct fs_file *file, int len) {
    if (len < 0) return false;
    file->data = sim_json_buffer;
    file->len = len;
    file->index = len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return true;
}

static bool http_rest_sim_pan(struct fs_file *file, int num_params,
                              char *params[], char *values[]) {
    for (int i = 0; i < num_params; i++) {
        if (strcmp(params[i], "action") == 0) {
            if (strcmp(values[i], "remove") == 0 || strcmp(values[i], "empty") == 0) {
                sim_plant_empty_pan(&g_plant_state);
            } else if (strcmp(values[i], "zero") == 0) {
                sim_plant_zero(&g_plant_state);
            }
        }
    }
    int len = snprintf(sim_json_buffer, sizeof(sim_json_buffer),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
        "{\"true_weight_gn\":%.4f,\"reported_gn\":%.4f,\"stable\":%s}",
        g_plant_state.true_weight_gn, g_plant_state.reported_weight_gn,
        g_plant_state.stable ? "true" : "false");
    return sim_finalize(file, len);
}

static bool http_rest_sim_plant(struct fs_file *file, int num_params,
                                char *params[], char *values[]) {
    for (int i = 0; i < num_params; i++) {
        if (strcmp(params[i], "kernel") == 0)
            g_plant_cfg.kernel_weight_gn = strtof(values[i], NULL);
        else if (strcmp(params[i], "coarse_tail") == 0)
            g_plant_cfg.coarse_tail_gn = strtof(values[i], NULL);
        else if (strcmp(params[i], "fine_tail") == 0)
            g_plant_cfg.fine_tail_gn = strtof(values[i], NULL);
        else if (strcmp(params[i], "noise") == 0)
            g_plant_cfg.flow_noise_frac = strtof(values[i], NULL);
        else if (strcmp(params[i], "coarse_k") == 0)
            g_plant_cfg.coarse_flow_k = strtof(values[i], NULL);
        else if (strcmp(params[i], "fine_k") == 0)
            g_plant_cfg.fine_flow_k = strtof(values[i], NULL);
    }
    int len = snprintf(sim_json_buffer, sizeof(sim_json_buffer),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
        "{\"kernel\":%.4f,\"coarse_tail\":%.4f,\"fine_tail\":%.4f,"
        "\"noise\":%.3f,\"coarse_k\":%.4f,\"fine_k\":%.4f,"
        "\"true_weight_gn\":%.4f}",
        g_plant_cfg.kernel_weight_gn, g_plant_cfg.coarse_tail_gn,
        g_plant_cfg.fine_tail_gn, g_plant_cfg.flow_noise_frac,
        g_plant_cfg.coarse_flow_k, g_plant_cfg.fine_flow_k,
        g_plant_state.true_weight_gn);
    return sim_finalize(file, len);
}

// ---------------------------------------------------------------------------
// Charge state machine thread
// ---------------------------------------------------------------------------
// charge_mode_menu() is the same entry point the device uses; it loops
// through zero -> charge -> stabilise -> cup removal -> cup return forever.
static void charge_thread() {
    for (;;) {
        charge_mode_menu(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

static void setup_profile_defaults() {
    profile_t *p = profile_get_selected();
    if (p == nullptr) return;
    if (p->coarse_max_flow_speed_rps <= 0.0f) {
        snprintf(p->name, sizeof(p->name), "SimProfile");
        p->coarse_kp = 0.35f; p->coarse_ki = 0.0f; p->coarse_kd = 0.0f;
        p->fine_kp = 3.0f;    p->fine_ki = 0.0f;   p->fine_kd = 0.0f;
        p->coarse_min_flow_speed_rps = 0.30f;
        p->coarse_max_flow_speed_rps = 8.0f;
        p->fine_min_flow_speed_rps = 0.08f;
        p->fine_max_flow_speed_rps = 2.20f;
    }
}

int main(int argc, char **argv) {
    int port = 8080;
    std::string html = "../src/html/web_portal.html";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--html") && i + 1 < argc) html = argv[++i];
        else if (!strcmp(argv[i], "--kernel") && i + 1 < argc) g_plant_cfg.kernel_weight_gn = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "-vv")) g_sim_verbosity = 2;
        else {
            printf("OpenTrickler simulator (real-time / web GUI)\n\n");
            printf("  --port <n>     HTTP port (default 8080)\n");
            printf("  --html <path>  Path to web_portal.html\n");
            printf("  --kernel <gn>  Powder kernel weight (default 0.04)\n");
            printf("  -vv            Print every motor command to the console\n");
            return 1;
        }
    }

    sim_plant_defaults(&g_plant_cfg);
    sim_plant_reset(&g_plant_cfg, &g_plant_state, 1);
    sim_rt_start();

    // Bring up the firmware subsystems in the same order app.c does.
    profile_data_init();
    charge_mode_config_init();
    ai_tuning_init();
    setup_profile_defaults();

    // Publish the endpoints the web portal needs. These are the real firmware
    // handlers - the same code that answers requests on the device.
    rest_register_handler((char *)"/rest/charge_mode_config", http_rest_charge_mode_config);
    rest_register_handler((char *)"/rest/charge_mode_state", http_rest_charge_mode_state);
    rest_register_handler((char *)"/rest/profile_config", http_rest_profile_config);
    rest_register_handler((char *)"/rest/profile_summary", http_rest_profile_summary);
    rest_ai_tuning_init();

    // Simulator-only controls.
    rest_register_handler((char *)"/rest/sim/pan", http_rest_sim_pan);
    rest_register_handler((char *)"/rest/sim/plant", http_rest_sim_plant);

    sim_http_start(port, html.c_str());

    printf("\n=== OpenTrickler simulator (real time) ===\n");
    printf("  Web portal : http://localhost:%d\n", port);
    printf("  Serving    : %s\n", html.c_str());
    printf("  Endpoints  : %d registered\n", sim_http_route_count());
    printf("  Scale      : A&D FX-120i model, %.4f gn resolution\n",
           g_plant_cfg.scale_resolution_gn);
    printf("  Kernel     : %.4f gn\n\n", g_plant_cfg.kernel_weight_gn);
    printf("  Throws run in real time. Cup handling is simulated:\n");
    printf("    empty the pan : http://localhost:%d/rest/sim/pan?action=remove\n", port);
    printf("    tare the scale: http://localhost:%d/rest/sim/pan?action=zero\n", port);
    printf("  Ctrl-C to stop.\n\n");
    fflush(stdout);

    std::thread(charge_thread).detach();

    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
