// Display is headless in the simulator: every draw call is a no-op.
#ifndef SIM_U8G2_H_
#define SIM_U8G2_H_
#include <stdint.h>
#include <string.h>
typedef struct { int dummy; } u8g2_t;
typedef uint16_t u8g2_uint_t;
typedef struct { int dummy; } u8g2_cb_t;
#ifdef __cplusplus
extern "C" {
#endif
#define U8G2_R0 ((const u8g2_cb_t*)0)
#define U8G2_R1 ((const u8g2_cb_t*)0)
#define U8G2_R2 ((const u8g2_cb_t*)0)
#define U8G2_R3 ((const u8g2_cb_t*)0)
extern const uint8_t u8g2_font_helvB08_tr[];
extern const uint8_t u8g2_font_helvR08_tr[];
extern const uint8_t u8g2_font_helvR10_tr[];
extern const uint8_t u8g2_font_helvR12_tr[];
extern const uint8_t u8g2_font_courB08_tr[];
extern const uint8_t u8g2_font_courR08_tr[];
extern const uint8_t u8g2_font_helvB10_tr[];
extern const uint8_t u8g2_font_helvB12_tr[];
extern const uint8_t u8g2_font_5x7_tr[];
extern const uint8_t u8g2_font_6x13_tr[];
extern const uint8_t u8g2_font_profont22_tf[];
static inline void u8g2_ClearBuffer(u8g2_t *u) { (void)u; }
static inline void u8g2_SendBuffer(u8g2_t *u) { (void)u; }
static inline void u8g2_ClearDisplay(u8g2_t *u) { (void)u; }
static inline void u8g2_SetFont(u8g2_t *u, const uint8_t *f) { (void)u; (void)f; }
static inline void u8g2_DrawStr(u8g2_t *u, u8g2_uint_t x, u8g2_uint_t y, const char *s) { (void)u; (void)x; (void)y; (void)s; }
static inline u8g2_uint_t u8g2_GetStrWidth(u8g2_t *u, const char *s) { (void)u; return (u8g2_uint_t)(s ? 6 * (u8g2_uint_t)strlen(s) : 0); }
static inline u8g2_uint_t u8g2_GetDisplayWidth(u8g2_t *u) { (void)u; return 128; }
static inline u8g2_uint_t u8g2_GetDisplayHeight(u8g2_t *u) { (void)u; return 64; }
static inline void u8g2_DrawHLine(u8g2_t *u, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w) { (void)u; (void)x; (void)y; (void)w; }
static inline void u8g2_DrawVLine(u8g2_t *u, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t h) { (void)u; (void)x; (void)y; (void)h; }
static inline void u8g2_DrawBox(u8g2_t *u, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h) { (void)u; (void)x; (void)y; (void)w; (void)h; }
static inline void u8g2_DrawFrame(u8g2_t *u, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h) { (void)u; (void)x; (void)y; (void)w; (void)h; }
static inline void u8g2_DrawXBM(u8g2_t *u, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, const uint8_t *b) { (void)u; (void)x; (void)y; (void)w; (void)h; (void)b; }
static inline void u8g2_SetPowerSave(u8g2_t *u, int v) { (void)u; (void)v; }
static inline void u8g2_SetContrast(u8g2_t *u, int v) { (void)u; (void)v; }
static inline void u8g2_InitDisplay(u8g2_t *u) { (void)u; }
static inline void u8g2_SetDisplayRotation(u8g2_t *u, const u8g2_cb_t *cb) { (void)u; (void)cb; }
#ifdef __cplusplus
}
#endif
#endif
