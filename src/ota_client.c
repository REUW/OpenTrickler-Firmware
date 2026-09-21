#include "ota_client.h"

// These error-path snprintf() calls build human-readable diagnostic strings
// (e.g. "could not send request to %s" with a caller-supplied hostname) into
// fixed 128-byte buffers purely for display in the GUI/status endpoints.
// GCC's static -Wformat-truncation analysis can't prove a worst-case host
// name plus message text always fits, but snprintf() truncates safely (no
// UB, no overflow) either way, so a truncated diagnostic message is a
// harmless, acceptable outcome here -- not a real bug.
#pragma GCC diagnostic ignored "-Wformat-truncation"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "common.h"
#include "eeprom.h"
#include "ota_update.h"
#include "ota_tls.h"
#include "version.h"

// ---------------------------------------------------------------------------
// Config (persisted). Just the GitHub repo to check -- defaults to this
// fork's own repo so it works out of the box for anyone who downloads a
// release, while staying editable for forks.
// ---------------------------------------------------------------------------

#define OTA_CLIENT_GITHUB_HOST     "github.com"
#define OTA_CLIENT_GITHUB_PORT     443
#define OTA_CLIENT_MANIFEST_ASSET  "manifest.json"
#define OTA_CLIENT_BIN_ASSET       "app.bin"
#define OTA_CLIENT_MAX_REDIRECTS   5

static eeprom_update_config_t g_update_config;

static const eeprom_update_config_t default_eeprom_update_config = {
    // Leave update_config_rev at 0 -- see common.c's load_config(): a
    // nonzero first field is only meaningful for migrating pre-CRC32
    // configs, which this newly-added region never had.
    .owner = "thomaspember1990",
    .repo = "OpenTrickler-Firmware",
};

// ---------------------------------------------------------------------------
// Runtime status (RAM only, polled by the GUI via /rest/update_status)
// ---------------------------------------------------------------------------

typedef enum {
    UPDATE_CHECK_IDLE = 0,
    UPDATE_CHECK_IN_PROGRESS,
    UPDATE_CHECK_OK,
    UPDATE_CHECK_FAILED,
} update_check_state_t;

typedef enum {
    UPDATE_APPLY_IDLE = 0,
    UPDATE_APPLY_DOWNLOADING,
    UPDATE_APPLY_STAGED,
    UPDATE_APPLY_APPLYING,
    UPDATE_APPLY_FAILED,
} update_apply_state_t;

typedef struct {
    update_check_state_t check_state;
    char latest_version[40];
    char notes[192];
    uint32_t bin_size;
    uint32_t bin_crc32;
    bool manifest_valid;
    char check_error[128];

    update_apply_state_t apply_state;
    uint32_t apply_received;
    uint32_t apply_total;
    char apply_error[128];
} ota_client_status_t;

static ota_client_status_t g_status = {0};

typedef enum {
    OTA_CLIENT_CMD_CHECK = 0,
    OTA_CLIENT_CMD_APPLY,
} ota_client_cmd_t;

static QueueHandle_t g_ota_client_queue = NULL;

#define OTA_CLIENT_HEADER_BUF_LEN       768
#define OTA_CLIENT_MANIFEST_BUF_LEN     2048
#define OTA_CLIENT_REQUEST_BUF_LEN      320
#define OTA_CLIENT_URL_BUF_LEN          384

// ---------------------------------------------------------------------------
// Minimal HTTPS/1.1 GET client, following redirects. Body bytes are handed
// to a callback as they arrive so a downloaded firmware image never has to
// be buffered whole in RAM. See ota_tls.c for the TLS transport.
// ---------------------------------------------------------------------------

typedef bool (*ota_client_body_cb_t)(void *ctx, const uint8_t *data, size_t len);

// Tiny case-insensitive strstr -- header names come back with whatever
// casing the server chose (GitHub/Fastly commonly lower-case them).
static char *strcasestr_ota(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return (char *)p;
        }
    }
    return NULL;
}

static bool parse_https_url(const char *url, char *host_out, size_t host_len, char *path_out, size_t path_len) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    }
    else if (strncmp(p, "http://", 7) == 0) {
        // GitHub never redirects us to plain http in practice, but handle it
        // the same way rather than failing outright -- we always speak TLS.
        p += 7;
    }
    else {
        return false;
    }

    const char *slash = strchr(p, '/');
    size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hlen == 0 || hlen >= host_len) {
        return false;
    }
    memcpy(host_out, p, hlen);
    host_out[hlen] = '\0';

    if (slash == NULL) {
        if (path_len < 2) {
            return false;
        }
        path_out[0] = '/';
        path_out[1] = '\0';
    }
    else {
        size_t plen = strlen(slash);
        if (plen >= path_len) {
            return false;
        }
        memcpy(path_out, slash, plen + 1);
    }
    return true;
}

// Does one GET (following up to OTA_CLIENT_MAX_REDIRECTS redirects) and
// streams the final 200 response's body to body_cb. Returns true only on a
// fully-received 200 OK with a usable Content-Length.
static bool ota_client_https_get(const char *host, const char *path,
                                  ota_client_body_cb_t body_cb, void *body_ctx,
                                  uint32_t *out_content_length,
                                  char *err, size_t err_len) {
    char cur_host[128];
    char cur_path[OTA_CLIENT_URL_BUF_LEN];
    snprintf(cur_host, sizeof(cur_host), "%s", host);
    snprintf(cur_path, sizeof(cur_path), "%s", path);

    for (int hop = 0; hop <= OTA_CLIENT_MAX_REDIRECTS; hop++) {
        ota_tls_conn_t *conn = ota_tls_connect(cur_host, OTA_CLIENT_GITHUB_PORT, err, err_len);
        if (conn == NULL) {
            return false;
        }

        char request[OTA_CLIENT_REQUEST_BUF_LEN];
        int req_len = snprintf(request, sizeof(request),
                                "GET %s HTTP/1.1\r\n"
                                "Host: %s\r\n"
                                "User-Agent: OpenTrickler-OTA/1.0\r\n"
                                "Accept: */*\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                cur_path, cur_host);
        if (req_len < 0 || (size_t)req_len >= sizeof(request)) {
            snprintf(err, err_len, "request path too long");
            ota_tls_close(conn);
            return false;
        }

        if (ota_tls_write(conn, (const uint8_t *)request, (size_t)req_len) < 0) {
            snprintf(err, err_len, "could not send request to %s", cur_host);
            ota_tls_close(conn);
            return false;
        }

        char header_buf[OTA_CLIENT_HEADER_BUF_LEN];
        size_t header_len = 0;
        bool header_complete = false;
        size_t body_start_in_buf = 0;

        while (!header_complete) {
            if (header_len >= sizeof(header_buf) - 1) {
                snprintf(err, err_len, "response headers too large");
                ota_tls_close(conn);
                return false;
            }
            int n = ota_tls_read(conn, (uint8_t *)header_buf + header_len, sizeof(header_buf) - 1 - header_len);
            if (n < 0) {
                snprintf(err, err_len, "TLS error waiting for response headers from %s (-0x%04x)", cur_host, (unsigned int)-n);
                ota_tls_close(conn);
                return false;
            }
            if (n == 0) {
                snprintf(err, err_len, "connection to %s closed before headers completed", cur_host);
                ota_tls_close(conn);
                return false;
            }
            header_len += (size_t)n;
            header_buf[header_len] = '\0';

            char *terminator = strstr(header_buf, "\r\n\r\n");
            if (terminator != NULL) {
                header_complete = true;
                body_start_in_buf = (size_t)((terminator + 4) - header_buf);
            }
        }

        if (strncmp(header_buf, "HTTP/1.", 7) != 0) {
            snprintf(err, err_len, "not an HTTP response from %s", cur_host);
            ota_tls_close(conn);
            return false;
        }
        int status_code = atoi(header_buf + 9);

        if (status_code == 301 || status_code == 302 || status_code == 303 ||
            status_code == 307 || status_code == 308) {
            const char *needle = "\r\nLocation:";
            char *loc = strcasestr_ota(header_buf, needle);
            if (loc == NULL) {
                snprintf(err, err_len, "server returned HTTP %d from %s with no Location header", status_code, cur_host);
                ota_tls_close(conn);
                return false;
            }
            loc += strlen(needle);
            while (*loc == ' ') {
                loc++;
            }
            char loc_buf[OTA_CLIENT_URL_BUF_LEN];
            size_t li = 0;
            while (*loc != '\r' && *loc != '\n' && *loc != '\0' && li + 1 < sizeof(loc_buf)) {
                loc_buf[li++] = *loc++;
            }
            loc_buf[li] = '\0';

            ota_tls_close(conn);

            if (!parse_https_url(loc_buf, cur_host, sizeof(cur_host), cur_path, sizeof(cur_path))) {
                snprintf(err, err_len, "could not parse redirect URL");
                return false;
            }
            continue;  // follow the redirect with a fresh connection
        }

        if (status_code != 200) {
            snprintf(err, err_len, "server returned HTTP %d for %s%s", status_code, cur_host, cur_path);
            ota_tls_close(conn);
            return false;
        }

        long content_length = -1;
        {
            char *cl = strcasestr_ota(header_buf, "\r\nContent-Length:");
            if (cl != NULL) {
                content_length = strtol(cl + 17, NULL, 10);
            }
        }
        if (content_length < 0) {
            snprintf(err, err_len, "response from %s has no Content-Length (chunked responses aren't supported)", cur_host);
            ota_tls_close(conn);
            return false;
        }
        if (out_content_length != NULL) {
            *out_content_length = (uint32_t)content_length;
        }

        uint32_t received = 0;
        size_t leftover_len = header_len - body_start_in_buf;
        if (leftover_len > (uint32_t)content_length) {
            leftover_len = (size_t)content_length;
        }
        if (leftover_len > 0) {
            if (body_cb != NULL && !body_cb(body_ctx, (const uint8_t *)header_buf + body_start_in_buf, leftover_len)) {
                snprintf(err, err_len, "%s", "download aborted (see staging error)");
                ota_tls_close(conn);
                return false;
            }
            received += (uint32_t)leftover_len;
        }

        uint8_t recv_buf[1024];
        while (received < (uint32_t)content_length) {
            size_t want = (uint32_t)content_length - received;
            if (want > sizeof(recv_buf)) {
                want = sizeof(recv_buf);
            }
            int n = ota_tls_read(conn, recv_buf, want);
            if (n < 0) {
                snprintf(err, err_len, "TLS error while downloading body from %s (-0x%04x)", cur_host, (unsigned int)-n);
                ota_tls_close(conn);
                return false;
            }
            if (n == 0) {
                snprintf(err, err_len, "connection closed early (%" PRIu32 "/%ld bytes) from %s", received, content_length, cur_host);
                ota_tls_close(conn);
                return false;
            }
            if (body_cb != NULL && !body_cb(body_ctx, recv_buf, (size_t)n)) {
                snprintf(err, err_len, "%s", "download aborted (see staging error)");
                ota_tls_close(conn);
                return false;
            }
            received += (uint32_t)n;
        }

        ota_tls_close(conn);
        return true;
    }

    snprintf(err, err_len, "too many redirects (>%d) fetching %s%s", OTA_CLIENT_MAX_REDIRECTS, host, path);
    return false;
}

// ---------------------------------------------------------------------------
// Tiny manifest.json field extractor -- our own fixed format (see
// QUICKSTART.md):
//   {"version":"2026.09.21-fork.5","notes":"...","size":123456,
//    "crc32":"0xABCD1234"}
// ---------------------------------------------------------------------------

static bool json_find_value_start(const char *json, const char *key, const char **out_start) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *key_pos = strstr(json, needle);
    if (key_pos == NULL) {
        return false;
    }
    const char *colon = strchr(key_pos + strlen(needle), ':');
    if (colon == NULL) {
        return false;
    }
    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    *out_start = p;
    return true;
}

static bool json_extract_string(const char *json, const char *key, char *out, size_t out_len) {
    const char *p;
    if (!json_find_value_start(json, key, &p) || *p != '"') {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1 < out_len) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return *p == '"';
}

static bool json_extract_u32(const char *json, const char *key, uint32_t *out) {
    const char *p;
    if (!json_find_value_start(json, key, &p)) {
        return false;
    }
    if (*p == '"') {
        p++;
    }
    char *end = NULL;
    unsigned long v = strtoul(p, &end, 0);
    if (end == p) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

// ---------------------------------------------------------------------------
// Check / apply worker
// ---------------------------------------------------------------------------

static bool ota_client_manifest_body_cb(void *ctx, const uint8_t *data, size_t len) {
    struct {
        char *buf;
        size_t buf_len;
        size_t written;
    } *state = ctx;

    size_t space = (state->buf_len > state->written) ? (state->buf_len - 1 - state->written) : 0;
    size_t copy_len = (len < space) ? len : space;
    memcpy(state->buf + state->written, data, copy_len);
    state->written += copy_len;
    state->buf[state->written] = '\0';
    return true;  // never fail the transfer just because the manifest is longer than our buffer
}

static void ota_client_build_asset_path(char *out, size_t out_len, const char *asset_name) {
    snprintf(out, out_len, "/%s/%s/releases/latest/download/%s",
             g_update_config.owner, g_update_config.repo, asset_name);
}

static void ota_client_do_check(void) {
    g_status.check_state = UPDATE_CHECK_IN_PROGRESS;

    if (g_update_config.owner[0] == '\0' || g_update_config.repo[0] == '\0') {
        g_status.check_state = UPDATE_CHECK_FAILED;
        g_status.manifest_valid = false;
        snprintf(g_status.check_error, sizeof(g_status.check_error), "GitHub repository is not configured");
        return;
    }

    static char manifest_buf[OTA_CLIENT_MANIFEST_BUF_LEN];
    struct {
        char *buf;
        size_t buf_len;
        size_t written;
    } state = { manifest_buf, sizeof(manifest_buf), 0 };
    manifest_buf[0] = '\0';

    char path[OTA_CLIENT_URL_BUF_LEN];
    ota_client_build_asset_path(path, sizeof(path), OTA_CLIENT_MANIFEST_ASSET);

    char err[128] = {0};
    bool ok = ota_client_https_get(OTA_CLIENT_GITHUB_HOST, path,
                                    ota_client_manifest_body_cb, &state,
                                    NULL, err, sizeof(err));
    if (!ok) {
        g_status.check_state = UPDATE_CHECK_FAILED;
        g_status.manifest_valid = false;
        snprintf(g_status.check_error, sizeof(g_status.check_error), "%s", err);
        return;
    }

    bool have_version = json_extract_string(manifest_buf, "version", g_status.latest_version, sizeof(g_status.latest_version));
    bool have_size = json_extract_u32(manifest_buf, "size", &g_status.bin_size);
    bool have_crc = json_extract_u32(manifest_buf, "crc32", &g_status.bin_crc32);
    json_extract_string(manifest_buf, "notes", g_status.notes, sizeof(g_status.notes));  // optional

    if (!have_version || !have_size || !have_crc) {
        g_status.check_state = UPDATE_CHECK_FAILED;
        g_status.manifest_valid = false;
        snprintf(g_status.check_error, sizeof(g_status.check_error),
                 "manifest.json is missing required fields (need version, size, crc32)");
        return;
    }

    g_status.manifest_valid = true;
    g_status.check_state = UPDATE_CHECK_OK;
    g_status.check_error[0] = '\0';
}

static bool ota_client_apply_body_cb(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    if (!ota_stage_write(g_status.apply_received, data, len)) {
        snprintf(g_status.apply_error, sizeof(g_status.apply_error), "%s", ota_stage_last_error());
        return false;
    }
    g_status.apply_received += (uint32_t)len;
    return true;
}

static void ota_client_do_apply(void) {
    if (!g_status.manifest_valid) {
        g_status.apply_state = UPDATE_APPLY_FAILED;
        snprintf(g_status.apply_error, sizeof(g_status.apply_error), "run Check for Updates first");
        return;
    }

    g_status.apply_state = UPDATE_APPLY_DOWNLOADING;
    g_status.apply_received = 0;
    g_status.apply_total = g_status.bin_size;
    g_status.apply_error[0] = '\0';

    if (!ota_stage_supported()) {
        g_status.apply_state = UPDATE_APPLY_FAILED;
        snprintf(g_status.apply_error, sizeof(g_status.apply_error),
                 "OTA staging is not supported on this flash layout");
        return;
    }

    if (!ota_stage_begin(g_status.bin_size, g_status.bin_crc32)) {
        g_status.apply_state = UPDATE_APPLY_FAILED;
        snprintf(g_status.apply_error, sizeof(g_status.apply_error), "%s", ota_stage_last_error());
        return;
    }

    char path[OTA_CLIENT_URL_BUF_LEN];
    ota_client_build_asset_path(path, sizeof(path), OTA_CLIENT_BIN_ASSET);

    uint32_t content_length = 0;
    char err[128] = {0};
    bool ok = ota_client_https_get(OTA_CLIENT_GITHUB_HOST, path,
                                    ota_client_apply_body_cb, NULL,
                                    &content_length, err, sizeof(err));
    if (!ok) {
        ota_stage_abort();
        g_status.apply_state = UPDATE_APPLY_FAILED;
        if (g_status.apply_error[0] == '\0') {
            snprintf(g_status.apply_error, sizeof(g_status.apply_error), "%s", err);
        }
        return;
    }

    if (content_length != g_status.bin_size) {
        ota_stage_abort();
        g_status.apply_state = UPDATE_APPLY_FAILED;
        snprintf(g_status.apply_error, sizeof(g_status.apply_error),
                 "downloaded size (%" PRIu32 ") didn't match manifest size (%" PRIu32 ")",
                 content_length, g_status.bin_size);
        return;
    }

    uint32_t final_crc = 0;
    if (!ota_stage_finalize(&final_crc)) {
        g_status.apply_state = UPDATE_APPLY_FAILED;
        snprintf(g_status.apply_error, sizeof(g_status.apply_error), "%s", ota_stage_last_error());
        return;
    }

    g_status.apply_state = UPDATE_APPLY_STAGED;

    if (!ota_stage_apply()) {
        g_status.apply_state = UPDATE_APPLY_FAILED;
        snprintf(g_status.apply_error, sizeof(g_status.apply_error), "%s", ota_stage_last_error());
        return;
    }

    // ota_stage_apply() schedules a background task that erases/programs the
    // primary slot and reboots the device after a short delay; nothing more
    // to do here.
    g_status.apply_state = UPDATE_APPLY_APPLYING;
}

static void ota_client_task(void *param) {
    (void)param;
    ota_client_cmd_t cmd;

    while (true) {
        if (xQueueReceive(g_ota_client_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd) {
                case OTA_CLIENT_CMD_CHECK:
                    ota_client_do_check();
                    break;
                case OTA_CLIENT_CMD_APPLY:
                    ota_client_do_apply();
                    break;
                default:
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Init / config persistence
// ---------------------------------------------------------------------------

bool ota_client_config_save(void) {
    return save_config(EEPROM_UPDATE_CONFIG_BASE_ADDR, &g_update_config, sizeof(g_update_config));
}

bool ota_client_init(void) {
    memset(&g_update_config, 0x00, sizeof(g_update_config));
    bool is_ok = load_config(EEPROM_UPDATE_CONFIG_BASE_ADDR, &g_update_config,
                              &default_eeprom_update_config, sizeof(g_update_config),
                              EEPROM_UPDATE_CONFIG_REV);
    if (!is_ok) {
        printf("Unable to read update-check configuration\n");
        g_update_config = default_eeprom_update_config;
    }

    memset(&g_status, 0, sizeof(g_status));

    g_ota_client_queue = xQueueCreate(2, sizeof(ota_client_cmd_t));
    // mbedTLS handshakes use fairly deep call stacks (bignum/RSA/ECC math),
    // so this task gets a much larger stack than the other small helper
    // tasks in this codebase.
    xTaskCreate(ota_client_task, "OTA Client Task", 8192, NULL, 3, NULL);

    eeprom_register_handler(ota_client_config_save);

    return is_ok;
}

// ---------------------------------------------------------------------------
// REST handlers
// ---------------------------------------------------------------------------

bool http_rest_update_config(struct fs_file *file, int num_params, char *params[], char *values[]) {
    // Mapping
    // u0 (str): owner
    // u1 (str): repo
    // ee (bool): save to eeprom
    static char json_buffer[384];
    bool save_to_eeprom = false;

    for (int idx = 0; idx < num_params; idx += 1) {
        if (strcmp(params[idx], "u0") == 0) {
            strncpy(g_update_config.owner, values[idx], sizeof(g_update_config.owner) - 1);
            g_update_config.owner[sizeof(g_update_config.owner) - 1] = '\0';
        }
        else if (strcmp(params[idx], "u1") == 0) {
            strncpy(g_update_config.repo, values[idx], sizeof(g_update_config.repo) - 1);
            g_update_config.repo[sizeof(g_update_config.repo) - 1] = '\0';
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_eeprom = string_to_boolean(values[idx]);
        }
    }

    if (save_to_eeprom) {
        if (!ota_client_config_save()) {
            snprintf(json_buffer, sizeof(json_buffer), "%s{\"error\":\"UpdateConfigSaveFailed\"}", http_json_header);
            file->data = json_buffer;
            file->len = strlen(json_buffer);
            file->index = file->len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
            return true;
        }
    }

    snprintf(json_buffer, sizeof(json_buffer),
             "%s{\"u0\":\"%s\",\"u1\":\"%s\",\"current_version\":\"%s\"}",
             http_json_header,
             g_update_config.owner,
             g_update_config.repo,
             version_string);

    file->data = json_buffer;
    file->len = strlen(json_buffer);
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return true;
}

bool http_rest_update_check(struct fs_file *file, int num_params, char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;
    static char json_buffer[160];

    if (g_status.check_state == UPDATE_CHECK_IN_PROGRESS) {
        snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":true,\"message\":\"check already in progress\"}", http_json_header);
    }
    else if (g_update_config.owner[0] == '\0' || g_update_config.repo[0] == '\0') {
        snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":false,\"message\":\"GitHub repository is not configured\"}", http_json_header);
    }
    else {
        ota_client_cmd_t cmd = OTA_CLIENT_CMD_CHECK;
        if (xQueueSend(g_ota_client_queue, &cmd, 0) == pdTRUE) {
            g_status.check_state = UPDATE_CHECK_IN_PROGRESS;
            snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":true,\"message\":\"check started\"}", http_json_header);
        }
        else {
            snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":false,\"message\":\"update client is busy\"}", http_json_header);
        }
    }

    file->data = json_buffer;
    file->len = strlen(json_buffer);
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return true;
}

bool http_rest_update_apply(struct fs_file *file, int num_params, char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;
    static char json_buffer[160];

    if (g_status.apply_state == UPDATE_APPLY_DOWNLOADING || g_status.apply_state == UPDATE_APPLY_APPLYING) {
        snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":true,\"message\":\"update already in progress\"}", http_json_header);
    }
    else if (!g_status.manifest_valid) {
        snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":false,\"message\":\"run Check for Updates first\"}", http_json_header);
    }
    else {
        ota_client_cmd_t cmd = OTA_CLIENT_CMD_APPLY;
        if (xQueueSend(g_ota_client_queue, &cmd, 0) == pdTRUE) {
            g_status.apply_state = UPDATE_APPLY_DOWNLOADING;
            snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":true,\"message\":\"update started\"}", http_json_header);
        }
        else {
            snprintf(json_buffer, sizeof(json_buffer), "%s{\"success\":false,\"message\":\"update client is busy\"}", http_json_header);
        }
    }

    file->data = json_buffer;
    file->len = strlen(json_buffer);
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return true;
}

static const char *update_check_state_string(update_check_state_t s) {
    switch (s) {
        case UPDATE_CHECK_IDLE: return "idle";
        case UPDATE_CHECK_IN_PROGRESS: return "checking";
        case UPDATE_CHECK_OK: return "checked";
        case UPDATE_CHECK_FAILED: return "check_failed";
        default: return "idle";
    }
}

static const char *update_apply_state_string(update_apply_state_t s) {
    switch (s) {
        case UPDATE_APPLY_IDLE: return "idle";
        case UPDATE_APPLY_DOWNLOADING: return "downloading";
        case UPDATE_APPLY_STAGED: return "staged";
        case UPDATE_APPLY_APPLYING: return "applying";
        case UPDATE_APPLY_FAILED: return "failed";
        default: return "idle";
    }
}

bool http_rest_update_status(struct fs_file *file, int num_params, char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;
    static char json_buffer[900];

    bool update_available = g_status.manifest_valid &&
                             strcmp(g_status.latest_version, version_string) != 0;

    snprintf(json_buffer, sizeof(json_buffer),
             "%s"
             "{\"current_version\":\"%s\","
             "\"check_state\":\"%s\","
             "\"check_error\":\"%s\","
             "\"manifest_valid\":%s,"
             "\"update_available\":%s,"
             "\"latest_version\":\"%s\","
             "\"notes\":\"%s\","
             "\"bin_size\":%" PRIu32 ","
             "\"apply_state\":\"%s\","
             "\"apply_error\":\"%s\","
             "\"apply_received\":%" PRIu32 ","
             "\"apply_total\":%" PRIu32 "}",
             http_json_header,
             version_string,
             update_check_state_string(g_status.check_state),
             g_status.check_error,
             boolean_to_string(g_status.manifest_valid),
             boolean_to_string(update_available),
             g_status.latest_version,
             g_status.notes,
             g_status.bin_size,
             update_apply_state_string(g_status.apply_state),
             g_status.apply_error,
             g_status.apply_received,
             g_status.apply_total);

    file->data = json_buffer;
    file->len = strlen(json_buffer);
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return true;
}
