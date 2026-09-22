#include <string.h>
#include <stdio.h>
#include <math.h>

#include "profile.h"
#include "eeprom.h"
#include "common.h"
#include "charge_mode.h"


eeprom_profile_data_t profile_data;

extern void swuart_calcCRC(uint8_t* datagram, uint8_t datagramLength);

const eeprom_profile_data_t default_profile_data = {
    .profile_data_rev = 0,
    .profiles[0] = {
        .compatibility = 0,
        .name = "AR2208,gr",

        .coarse_kp = 0.025f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 5.0f,

        .fine_kp = 2.0f,
        .fine_ki = 0.0f,
        .fine_kd = 10.0f,
        .fine_min_flow_speed_rps = 0.08f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[1] = {
        .compatibility = 0,
        .name = "AR2209,gr",

        .coarse_kp = 0.05f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 8.0f,

        .fine_kp = 0.8f,
        .fine_ki = 0.0f,
        .fine_kd = 15.0f,
        .fine_min_flow_speed_rps = 0.08f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[2] = {
        .compatibility = 0,
        .name = "8208XBR,gr",

        .coarse_kp = 0.05f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 5.0f,

        .fine_kp = 2.0f,
        .fine_ki = 0.0f,
        .fine_kd = 12.0f,
        .fine_min_flow_speed_rps = 0.06f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[3] = {
        .compatibility = 0,
        .name = "Benchmark2,gr",

        .coarse_kp = 0.06f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 5.0f,

        .fine_kp = 0.8f,
        .fine_ki = 0.0f,
        .fine_kd = 15.0f,
        .fine_min_flow_speed_rps = 0.08f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[4] = {
        .compatibility = 0,
        .name = "Profile4",
    },
    .profiles[5] = {
        .compatibility = 0,
        .name = "Profile5",
    },
    .profiles[6] = {
        .compatibility = 0,
        .name = "Profile6",
    },
    .profiles[7] = {
        .compatibility = 0,
        .name = "Profile7",
    },
};


/*
 * Escape a string for embedding inside a JSON string value.
 * Handles ", \, and common control characters. Output is always NUL-terminated
 * when out_len > 0.
 */
static void json_escape_string(const char *in, char *out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return;
    }
    if (in == NULL) {
        out[0] = '\0';
        return;
    }

    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && (o + 1) < out_len; i++) {
        unsigned char c = (unsigned char)in[i];
        const char *esc = NULL;

        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                break;
        }

        if (esc != NULL) {
            for (size_t j = 0; esc[j] != '\0' && (o + 1) < out_len; j++) {
                out[o++] = esc[j];
            }
        } else if (c < 0x20) {
            // Other control chars as \u00XX
            if ((o + 6) < out_len) {
                int n = snprintf(&out[o], out_len - o, "\\u%04x", c);
                if (n > 0) {
                    o += (size_t)n;
                }
            } else {
                break;
            }
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}


/*
 * Format a float for JSON. Non-finite values become null so the client
 * never sees bare "nan" / "inf" tokens (invalid JSON).
 */
static void json_format_float(char *out, size_t out_len, float value, int decimals) {
    if (out == NULL || out_len == 0) {
        return;
    }
    if (!isfinite(value)) {
        snprintf(out, out_len, "null");
        return;
    }
    snprintf(out, out_len, "%.*f", decimals, (double)value);
}


bool profile_data_save(void) {
    bool is_ok = save_config(EEPROM_PROFILE_DATA_BASE_ADDR, &profile_data, sizeof(profile_data));
    return is_ok;
}


bool profile_data_init(void) {
    bool is_ok = true;

    // Read profile index table
    memset(&profile_data, 0x0, sizeof(eeprom_profile_data_t));
    is_ok = load_config(EEPROM_PROFILE_DATA_BASE_ADDR, &profile_data, &default_profile_data, sizeof(profile_data), EEPROM_PROFILE_DATA_REV);

    if (!is_ok) {
        printf("Unable to read profile data\n");
        return false;
    }

    // Register to eeprom save all
    eeprom_register_handler(profile_data_save);

    return true;
}


uint16_t profile_get_selected_idx(void) {
    return profile_data.current_profile_idx;
}

bool profile_get_idx_for_pointer(const profile_t *profile, uint8_t *idx_out) {
    if (profile == NULL || idx_out == NULL) {
        return false;
    }

    for (uint8_t idx = 0; idx < MAX_PROFILE_CNT; idx++) {
        if (&profile_data.profiles[idx] == profile) {
            *idx_out = idx;
            return true;
        }
    }

    return false;
}


profile_t * profile_get_selected(void) {
    if (profile_data.current_profile_idx >= MAX_PROFILE_CNT) {
        profile_data.current_profile_idx = 0;
    }

    return &profile_data.profiles[profile_get_selected_idx()];
}


// Read-only lookup of any profile slot by index, regardless of which one is
// currently selected/active. Used by the AI suggestion engine, which needs
// to know a profile's *currently configured* min flow speeds (the hard floor
// the PID loop will actually command) even when suggesting for a profile
// that is not the one presently loaded.
profile_t * profile_get_by_idx(uint8_t idx) {
    if (idx >= MAX_PROFILE_CNT) {
        return NULL;
    }

    return &profile_data.profiles[idx];
}


profile_t * profile_select(uint8_t idx) {
    if (idx >= MAX_PROFILE_CNT) {
        return NULL;
    }

    profile_data.current_profile_idx = idx;

    // Charge mode settings are per-profile: load the new profile's settings
    // into the active working copy so subsequent charges use them.
    charge_mode_data_load_for_profile(idx);

    return profile_get_selected();
}


void profile_update_checksum() {
    swuart_calcCRC((uint8_t *) profile_get_selected(), sizeof(profile_t));
}

bool http_rest_profile_config(struct fs_file *file, int num_params, char *params[], char *values[]) {
    // Mappings:
    // pf (int): profile index
    // p0 (int): rev
    // p1 (int): compatibility
    // p2 (str): name
    // p3 (float): coarse_kp
    // p4 (float): coarse_ki
    // p5 (float): coarse_kd
    // p6 (float): coarse_min_flow_speed_rps
    // p7 (float): coarse_max_flow_speed_rps
    // p8 (float): fine_kp
    // p9 (float): fine_ki
    // p10 (float): fine_kd
    // p11 (float): fine_min_flow_speed_rps
    // p12 (float): fine_max_flow_speed_rps
    // active/select (bool): make this the active runtime profile
    // read_only (bool): inspect profile without changing active profile
    // ee (bool): save to eeprom

    // Was 256 and truncated the HTTP+JSON response, which caused
    // JSON.parse failures in the web UI (Suggested PID Baseline).
    static char buf[512];

    // Read the current loaded profile index
    uint8_t profile_idx = profile_get_selected_idx();
    bool has_profile_idx = false;
    bool read_only = false;
    bool activate_profile = false;
    bool save_to_eeprom = false;

    // Overwrite the profile index (if applicable)
    for (int idx = 0; idx < num_params; idx += 1) {
        if (strcmp(params[idx], "pf") == 0) {
            profile_idx = (uint16_t) atoi(values[idx]);
            has_profile_idx = true;
        }
        else if (strcmp(params[idx], "active") == 0 ||
                 strcmp(params[idx], "select") == 0) {
            activate_profile = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "read_only") == 0) {
            read_only = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_eeprom = string_to_boolean(values[idx]);
        }
    }

    if (profile_idx >= MAX_PROFILE_CNT) {
        snprintf(buf, sizeof(buf), "%s{\"error\":\"InvalidProfileIndex\"}", http_json_header);
    }

    else {
        /*
         * Backward compatibility: historically /rest/profile_config?pf=N
         * selected N as a side effect. Keep that behavior unless callers
         * explicitly request read_only=true for export/inspection.
         */
        if (read_only) {
            activate_profile = false;
        }
        else if (has_profile_idx) {
            activate_profile = true;
        }

        profile_t * current_profile = activate_profile
            ? profile_select(profile_idx)
            : &profile_data.profiles[profile_idx];
        if (current_profile == NULL) {
            snprintf(buf, sizeof(buf), "%s{\"error\":\"InvalidProfileIndex\"}", http_json_header);
            size_t response_len = strlen(buf);
            file->data = buf;
            file->len = response_len;
            file->index = response_len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
            return true;
        }

        if (!read_only) {
            // Control
            for (int idx = 0; idx < num_params; idx += 1) {
                if (strcmp(params[idx], "p0") == 0) {
                    current_profile->rev = strtol(values[idx], NULL, 10);
                }
                else if (strcmp(params[idx], "p1") == 0) {
                    current_profile->compatibility = strtol(values[idx], NULL, 10);
                }
                else if (strcmp(params[idx], "p2") == 0) {
                    strncpy(current_profile->name, values[idx], sizeof(current_profile->name) - 1);
                    current_profile->name[sizeof(current_profile->name) - 1] = '\0';
                }
                else if (strcmp(params[idx], "p3") == 0) {
                    current_profile->coarse_kp = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p4") == 0) {
                    current_profile->coarse_ki = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p5") == 0) {
                    current_profile->coarse_kd = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p6") == 0) {
                    current_profile->coarse_min_flow_speed_rps = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p7") == 0) {
                    current_profile->coarse_max_flow_speed_rps = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p8") == 0) {
                    current_profile->fine_kp = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p9") == 0) {
                    current_profile->fine_ki = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p10") == 0) {
                    current_profile->fine_kd = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p11") == 0) {
                    current_profile->fine_min_flow_speed_rps = strtof(values[idx], NULL);
                }
                else if (strcmp(params[idx], "p12") == 0) {
                    current_profile->fine_max_flow_speed_rps = strtof(values[idx], NULL);
                }
            }
        }

        // Perform action
        if (save_to_eeprom && !read_only) {
            if (!profile_data_save()) {
                snprintf(buf, sizeof(buf), "%s{\"error\":\"ProfileSaveFailed\"}", http_json_header);
                size_t response_len = strlen(buf);
                file->data = buf;
                file->len = response_len;
                file->index = response_len;
                file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
                return true;
            }
        }

        // Escape name and format floats so the response is always valid JSON.
        // PROFILE_NAME_MAX_LEN is 16; escaped worst-case needs more room.
        char escaped_name[PROFILE_NAME_MAX_LEN * 6 + 1];
        json_escape_string(current_profile->name, escaped_name, sizeof(escaped_name));

        char f_p3[24], f_p4[24], f_p5[24], f_p6[24], f_p7[24];
        char f_p8[24], f_p9[24], f_p10[24], f_p11[24], f_p12[24];
        json_format_float(f_p3,  sizeof(f_p3),  current_profile->coarse_kp, 3);
        json_format_float(f_p4,  sizeof(f_p4),  current_profile->coarse_ki, 3);
        json_format_float(f_p5,  sizeof(f_p5),  current_profile->coarse_kd, 3);
        json_format_float(f_p6,  sizeof(f_p6),  current_profile->coarse_min_flow_speed_rps, 3);
        json_format_float(f_p7,  sizeof(f_p7),  current_profile->coarse_max_flow_speed_rps, 3);
        json_format_float(f_p8,  sizeof(f_p8),  current_profile->fine_kp, 3);
        json_format_float(f_p9,  sizeof(f_p9),  current_profile->fine_ki, 3);
        json_format_float(f_p10, sizeof(f_p10), current_profile->fine_kd, 3);
        json_format_float(f_p11, sizeof(f_p11), current_profile->fine_min_flow_speed_rps, 3);
        json_format_float(f_p12, sizeof(f_p12), current_profile->fine_max_flow_speed_rps, 3);

        int written = snprintf(buf, sizeof(buf),
                 "%s"
                 "{\"pf\":%d,\"p0\":%ld,\"p1\":%ld,\"p2\":\"%s\","
                 "\"p3\":%s,\"p4\":%s,\"p5\":%s,\"p6\":%s,\"p7\":%s,"
                 "\"p8\":%s,\"p9\":%s,\"p10\":%s,\"p11\":%s,\"p12\":%s}",
                 http_json_header,
                 profile_idx,
                 (long)current_profile->rev,
                 (long)current_profile->compatibility,
                 escaped_name,
                 f_p3, f_p4, f_p5, f_p6, f_p7,
                 f_p8, f_p9, f_p10, f_p11, f_p12);

        if (written < 0 || written >= (int)sizeof(buf)) {
            // Should not happen with a 512-byte buffer; fail safe.
            snprintf(buf, sizeof(buf), "%s{\"error\":\"ResponseTooLarge\"}", http_json_header);
        }
    }

    size_t response_len = strlen(buf);
    file->data = buf;
    file->len = response_len;
    file->index = response_len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}


bool http_rest_profile_summary(struct fs_file *file, int num_params, char *params[], char *values[])
{
    // It does not take argument
    assert(MAX_PROFILE_CNT <= 8);
    // Was 256; with escaped names and header this was tight. 512 is safer.
    static char buf[512];

    // Response
    // s0 (dict): A dictionary of all profiles in {idx: name} format.
    // s1 (int): The current loaded profile index
    memset(buf, 0x0, sizeof(buf));

    // Create header
    int written = snprintf(buf, sizeof(buf),
             "%s{\"s0\":{",
             http_json_header);
    if (written < 0 || written >= (int)sizeof(buf)) {
        snprintf(buf, sizeof(buf), "%s{\"error\":\"ResponseTooLarge\"}", http_json_header);
        size_t response_len = strlen(buf);
        file->data = buf;
        file->len = response_len;
        file->index = response_len;
        file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        return true;
    }

    size_t char_idx = (size_t)written;

    // Write profile information with escaped names
    for (uint8_t p_idx = 0; p_idx < MAX_PROFILE_CNT; p_idx += 1) {
        char escaped_name[PROFILE_NAME_MAX_LEN * 6 + 1];
        json_escape_string(profile_data.profiles[p_idx].name, escaped_name, sizeof(escaped_name));

        written = snprintf(&buf[char_idx], sizeof(buf) - char_idx,
                 "\"%d\":\"%s\",",
                 p_idx, escaped_name);
        if (written < 0 || written >= (int)(sizeof(buf) - char_idx)) {
            snprintf(buf, sizeof(buf), "%s{\"error\":\"ResponseTooLarge\"}", http_json_header);
            size_t response_len = strlen(buf);
            file->data = buf;
            file->len = response_len;
            file->index = response_len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
            return true;
        }
        char_idx += (size_t)written;
    }

    // Append close bracket (replace the last comma)
    if (char_idx > 0) {
        buf[char_idx - 1] = '}';
    }

    // Append s1
    written = snprintf(&buf[char_idx], sizeof(buf) - char_idx,
             ",\"s1\":%d}",
             profile_data.current_profile_idx);
    if (written < 0 || written >= (int)(sizeof(buf) - char_idx)) {
        snprintf(buf, sizeof(buf), "%s{\"error\":\"ResponseTooLarge\"}", http_json_header);
    }

    size_t response_len = strlen(buf);
    file->data = buf;
    file->len = response_len;
    file->index = response_len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}