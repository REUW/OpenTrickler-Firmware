#include "ota_debug.h"

#include "eeprom.h"

void ota_debug_checkpoint(ota_debug_checkpoint_t cp) {
    uint8_t v = (uint8_t)cp;
    eeprom_write(EEPROM_OTA_DEBUG_CHECKPOINT_ADDR, &v, sizeof(v));
}

uint8_t ota_debug_last_boot_checkpoint(void) {
    uint8_t v = OTA_DEBUG_CP_NONE;
    if (!eeprom_read(EEPROM_OTA_DEBUG_CHECKPOINT_ADDR, &v, sizeof(v))) {
        return OTA_DEBUG_CP_NONE;
    }
    return v;
}

const char *ota_debug_checkpoint_label(uint8_t cp) {
    switch ((ota_debug_checkpoint_t)cp) {
        case OTA_DEBUG_CP_NONE:               return "none (no check has run since this EEPROM slot was last cleared)";
        case OTA_DEBUG_CP_CHECK_STARTED:      return "check started";
        case OTA_DEBUG_CP_TLS_GLOBAL_INIT:    return "seeding RNG / parsing embedded CA bundle";
        case OTA_DEBUG_CP_TLS_GLOBAL_INIT_OK: return "RNG + CA bundle ready";
        case OTA_DEBUG_CP_DNS_RESOLVE:        return "resolving github.com";
        case OTA_DEBUG_CP_TCP_CONNECT:        return "opening TCP connection";
        case OTA_DEBUG_CP_TCP_CONNECTED:      return "TCP connected";
        case OTA_DEBUG_CP_SSL_SETUP:          return "setting up TLS session";
        case OTA_DEBUG_CP_TLS_HANDSHAKE:      return "starting TLS handshake";
        case OTA_DEBUG_CP_TLS_HANDSHAKE_OK:   return "TLS handshake completed";
        case OTA_DEBUG_CP_CERT_VERIFIED:      return "certificate verified";
        case OTA_DEBUG_CP_SEND_REQUEST:       return "sending HTTP request";
        case OTA_DEBUG_CP_REQUEST_SENT:       return "HTTP request sent, waiting for response";
        case OTA_DEBUG_CP_READ_HEADERS:       return "reading response headers";
        case OTA_DEBUG_CP_HEADERS_OK:         return "response headers received";
        case OTA_DEBUG_CP_READ_BODY:          return "downloading response body";
        case OTA_DEBUG_CP_BODY_OK:            return "response body downloaded";
        case OTA_DEBUG_CP_MANIFEST_PARSED:    return "manifest.json parsed";
        case OTA_DEBUG_CP_CHECK_COMPLETE:     return "check completed normally";
        default:                              return "unknown";
    }
}
