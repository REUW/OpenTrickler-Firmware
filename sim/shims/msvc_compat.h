// MSVC compatibility shim, force-included on Visual Studio builds only.
//
// The firmware is written for GCC and uses a few GCC extensions that MSVC
// does not understand. Without this, MSVC parses __attribute__ as an
// identifier and produces a cascade of confusing errors in neopixel_led.h,
// eeprom.h and ai_tuning.h that have nothing to do with the real problem.
#ifndef SIM_MSVC_COMPAT_H_
#define SIM_MSVC_COMPAT_H_

#ifdef _MSC_VER

// Drop __attribute__((...)) entirely.
//
// NOTE ON PACKING: this discards __attribute__((packed)) on three structs
// (rgbw_u32_t, eeprom_metadata_t, ai_tuning_config_eeprom_t), so MSVC may
// insert padding where the firmware would not.
//
// This is safe for the simulator because its EEPROM and flash are plain
// in-memory arrays: the same layout is used to write and to read, and the
// CRC is computed over that same layout, so everything is self-consistent.
//
// It would NOT be safe if you tried to load a binary EEPROM image captured
// from a real device into the simulator, or vice versa - the byte offsets
// can differ. Do not rely on cross-compatibility of binary blobs between an
// MSVC simulator build and real hardware. The MinGW/GCC build does not have
// this caveat, since it honours the packed attribute.
#define __attribute__(x)

// MSVC spells these differently.
#ifndef __builtin_strlen
#define __builtin_strlen(s) strlen(s)
#endif

// The Pico SDK provides a bare "uint" typedef that the firmware headers use
// (motors.h pin numbers, among others). MSVC has no such type.
typedef unsigned int uint;

// isnanf / isinff are GNU float-specific spellings. MSVC only provides the
// type-generic isnan / isinf macros from <math.h>, which work correctly on
// floats, so alias to those.
#include <math.h>
#ifndef isnanf
#define isnanf(x) isnan(x)
#endif
#ifndef isinff
#define isinff(x) isinf(x)
#endif

// Quieten the security warnings for the C runtime functions the firmware
// uses (snprintf, strtof, etc.) - they are used correctly here.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <string.h>

// ---------------------------------------------------------------------------
// Global variable linkage
// ---------------------------------------------------------------------------
// GCC does not mangle the names of plain global variables, so a global
// defined in a .c file and referenced from a .cpp file (or vice versa) links
// without ceremony. The firmware relies on this in several places -
// charge_mode.cpp defines charge_mode_config and ai_tuning.c reads it,
// scale_config is defined in C and read from C++, and so on.
//
// MSVC *does* mangle global variable names in C++, so those references do not
// resolve and you get LNK2001 with a helpful "symbols that could potentially
// match" hint naming the unmangled symbol.
//
// Declaring each shared global inside extern "C" here, before any firmware
// header is parsed, forces C linkage on them. A later redeclaration without
// a linkage specification inherits it, so the firmware's own
// "extern scale_config_t scale_config;" lines pick this up automatically and
// no firmware source needs editing.
#ifdef __cplusplus

#include "FreeRTOS.h"
#include "queue.h"
#include "app_state.h"
#include "scale.h"
#include "servo_gate.h"
#include "neopixel_led.h"
#include "charge_mode.h"
#include "sim_plant.h"

extern "C" {
extern scale_config_t scale_config;
extern servo_gate_t servo_gate;
extern AppState_t exit_state;
extern QueueHandle_t encoder_event_queue;
extern neopixel_led_config_t neopixel_led_config;
extern charge_mode_config_t charge_mode_config;
extern sim_plant_config_t g_plant_cfg;
extern sim_plant_state_t g_plant_state;
}

#endif  // __cplusplus

#endif  // _MSC_VER
#endif  // SIM_MSVC_COMPAT_H_
