#ifndef SIM_HW_UART_H_
#define SIM_HW_UART_H_
#include <stdint.h>
#include <stdbool.h>
typedef struct { int dummy; } *uart_inst_t;
#define uart0 ((uart_inst_t)0)
#define uart1 ((uart_inst_t)0)
static inline bool uart_is_readable(uart_inst_t u) { (void)u; return false; }
static inline char uart_getc(uart_inst_t u) { (void)u; return 0; }
static inline void uart_putc(uart_inst_t u, char c) { (void)u; (void)c; }
static inline void uart_puts(uart_inst_t u, const char *s) { (void)u; (void)s; }
static inline void uart_write_blocking(uart_inst_t u, const uint8_t *d, size_t n) { (void)u; (void)d; (void)n; }
static inline uint32_t uart_init(uart_inst_t u, uint32_t b) { (void)u; return b; }
static inline void uart_set_format(uart_inst_t u, int d, int s, int p) { (void)u; (void)d; (void)s; (void)p; }
#endif
