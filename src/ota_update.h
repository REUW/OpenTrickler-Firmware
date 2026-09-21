#ifndef OTA_UPDATE_H_
#define OTA_UPDATE_H_

#include <stdbool.h>

#include "http_rest.h"

#ifdef __cplusplus
extern "C" {
#endif

void ota_update_init(void);

// Internal staging API, shared by the REST hex-chunk handlers below and by
// ota_client.c (which streams a downloaded firmware image straight into the
// same staging area instead of round-tripping it through hex-encoded REST
// calls). Only one staging session (REST- or client-driven) is expected to
// be active at a time.
bool ota_stage_supported(void);
uint32_t ota_stage_capacity(void);
uint32_t ota_stage_primary_limit(void);
uint32_t ota_stage_chunk_size(void);
const char *ota_stage_last_error(void);
bool ota_stage_begin(uint32_t size, uint32_t expected_crc32);
bool ota_stage_write(uint32_t offset, const uint8_t *data, size_t len);
bool ota_stage_finalize(uint32_t *out_crc32);
bool ota_stage_apply(void);
void ota_stage_abort(void);

bool http_rest_ota_status(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_ota_begin(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_ota_chunk(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_ota_finalize(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_ota_abort(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_ota_apply(struct fs_file *file, int num_params, char *params[], char *values[]);

#ifdef __cplusplus
}
#endif

#endif  // OTA_UPDATE_H_
