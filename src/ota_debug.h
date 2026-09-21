#ifndef OTA_DEBUG_H_
#define OTA_DEBUG_H_

#include <stdint.h>
#include <stdbool.h>

// Crash-diagnosis aid for the update-check/apply path, for boards with no
// serial console attached. Each of these steps writes a single byte to a
// dedicated EEPROM address (fast enough to call often, and it survives a
// hard lockup + power cycle, unlike anything held only in RAM). After a
// freeze, http_rest_update_status reports the *previous* boot's last
// checkpoint (ota_debug_last_boot_checkpoint()) so the web page can show
// exactly how far the update check got before everything stopped
// responding -- no UART adapter required.
//
// Remove this (and its EEPROM_OTA_DEBUG_CHECKPOINT_ADDR slot in eeprom.h)
// once the TLS client is confirmed stable; it's a temporary diagnostic, not
// meant to ship long-term.

typedef enum {
    OTA_DEBUG_CP_NONE               = 0,
    OTA_DEBUG_CP_CHECK_STARTED      = 1,
    OTA_DEBUG_CP_TLS_GLOBAL_INIT    = 2,  // seeding DRBG + parsing the embedded CA bundle
    OTA_DEBUG_CP_TLS_GLOBAL_INIT_OK = 3,
    OTA_DEBUG_CP_DNS_RESOLVE        = 4,
    OTA_DEBUG_CP_TCP_CONNECT        = 5,
    OTA_DEBUG_CP_TCP_CONNECTED      = 6,
    OTA_DEBUG_CP_SSL_SETUP          = 7,
    OTA_DEBUG_CP_TLS_HANDSHAKE      = 8,  // about to call mbedtls_ssl_handshake()
    OTA_DEBUG_CP_TLS_HANDSHAKE_OK   = 9,
    OTA_DEBUG_CP_CERT_VERIFIED      = 10,
    OTA_DEBUG_CP_SEND_REQUEST       = 11,
    OTA_DEBUG_CP_REQUEST_SENT       = 12,
    OTA_DEBUG_CP_READ_HEADERS       = 13,
    OTA_DEBUG_CP_HEADERS_OK         = 14,
    OTA_DEBUG_CP_READ_BODY          = 15,
    OTA_DEBUG_CP_BODY_OK            = 16,
    OTA_DEBUG_CP_MANIFEST_PARSED    = 17,
    OTA_DEBUG_CP_CHECK_COMPLETE     = 18,
} ota_debug_checkpoint_t;

#ifdef __cplusplus
extern "C" {
#endif

// Best-effort: writes one byte to EEPROM. Cheap enough to call at every
// stage of the check/apply flow; failures are ignored (this is diagnostics,
// never allowed to be the reason a real check fails).
void ota_debug_checkpoint(ota_debug_checkpoint_t cp);

// Human-readable label for a checkpoint value, for display in the GUI.
const char *ota_debug_checkpoint_label(uint8_t cp);

// Reads whatever checkpoint value was left in EEPROM from *before this
// boot* -- call once at startup, before anything overwrites it. 0
// (OTA_DEBUG_CP_NONE) if nothing usable was found.
uint8_t ota_debug_last_boot_checkpoint(void);

#ifdef __cplusplus
}
#endif

#endif  // OTA_DEBUG_H_
