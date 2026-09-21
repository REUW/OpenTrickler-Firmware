#ifndef OTA_TLS_H_
#define OTA_TLS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Small blocking TLS-client wrapper used only by ota_client.c to reach
// github.com / *.githubusercontent.com over HTTPS. It rides on the same
// lwIP sockets API the rest of ota_client.c already uses (proven to work in
// this firmware, thread-safe by construction) rather than lwIP's raw/altcp
// callback API, and layers mbedTLS on top via a small custom BIO. Trust
// anchors come from ota_tls_roots.c; see that file for provenance.
//
// Every call here blocks (bounded by the socket's own SO_RCVTIMEO/SNDTIMEO)
// and is meant to be used from a single background task -- not reentrant,
// not safe to call from more than one task/connection at a time.

typedef struct ota_tls_conn ota_tls_conn_t;

#ifdef __cplusplus
extern "C" {
#endif

// Seeds the RNG and parses the embedded CA bundle. Called lazily by
// ota_tls_connect() on first use; safe to call more than once.
bool ota_tls_global_init(char *err, size_t err_len);

// Diagnostic only: a short summary of what actually ended up in the parsed
// trust store after ota_tls_global_init() ran -- "N loaded (F failed): "
// followed by each loaded cert's subject CN, comma-separated. Lets us
// confirm on a real device (no UART) whether every embedded cert actually
// parsed and made it into the in-memory chain mbedTLS verifies against,
// rather than assuming the source file's contents are what's running.
// Empty string until ota_tls_global_init() has run at least once.
const char *ota_tls_ca_chain_summary(void);

// Resolves host (dotted IP or DNS name), opens a TCP connection, and
// performs a full TLS 1.2 handshake with certificate verification against
// the embedded CA bundle and SNI/hostname checking against `host`. Returns
// NULL and fills err on any failure (DNS, connect, handshake, or cert
// verification).
ota_tls_conn_t *ota_tls_connect(const char *host, uint16_t port, char *err, size_t err_len);

// mbedtls_ssl_read() semantics: >0 bytes read, 0 on clean connection close,
// <0 (an MBEDTLS_ERR_* code) on error.
int ota_tls_read(ota_tls_conn_t *conn, uint8_t *buf, size_t len);

// Writes the whole buffer (looping over mbedtls_ssl_write() as needed).
// Returns the number of bytes written (== len on success) or a negative
// MBEDTLS_ERR_* code on failure.
int ota_tls_write(ota_tls_conn_t *conn, const uint8_t *buf, size_t len);

// Sends a close_notify and releases the connection's resources. Safe to
// call with NULL.
void ota_tls_close(ota_tls_conn_t *conn);

#ifdef __cplusplus
}
#endif

#endif  // OTA_TLS_H_
