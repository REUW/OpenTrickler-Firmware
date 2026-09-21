#include "ota_tls.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"  // xPortGetFreeHeapSize() -- diagnostic use in ota_tls_global_init()

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
// Only used for its MBEDTLS_ERR_NET_* error-code macros (plain #defines, not
// gated by MBEDTLS_NET_C) -- we never call into net_sockets.c's own
// functions, since our BIO talks to lwIP sockets directly.
#include "mbedtls/net_sockets.h"

#include "ota_tls_roots.h"
#include "ota_debug.h"

struct ota_tls_conn {
    int sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
};

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr_drbg;
static mbedtls_x509_crt g_ca_chain;
static bool g_global_ready = false;

// Diagnostic only: see ota_tls_ca_chain_summary() / ota_tls_global_init().
static char g_ca_chain_summary[600] = {0};

const char *ota_tls_ca_chain_summary(void) {
    return g_ca_chain_summary;
}

// mbedtls_x509_dn_gets() backslash-escapes special characters (notably a
// literal ',' inside an RDN value, e.g. "DigiCert\, Inc.") so the ", "
// between RDNs stays unambiguous -- correct for its own purposes, but both
// diagnostic strings below get embedded directly into a JSON string value
// in ota_client.c's /rest/update_status output with no further escaping, so
// a stray '\' there breaks the JSON (not a valid JSON escape) and a stray
// '"' would terminate the string early. Strip both in place so anything
// mbedTLS hands back is always safe to drop straight into that JSON field.
static void ota_tls_sanitize_for_json(char *s) {
    char *w = s;
    for (char *r = s; *r != '\0'; r++) {
        if (*r != '\\' && *r != '"') {
            *w++ = *r;
        }
    }
    *w = '\0';
}

// Diagnostic only: filled in by ota_tls_verify_cb() as mbedTLS walks the
// server's certificate chain during verification, one entry per cert. This
// is the one place the chain is guaranteed to still be intact regardless of
// whether verification ultimately passes or fails, since it runs *during*
// the walk rather than after the handshake has already aborted and torn
// state down. Used only to report the actual chain a server sent when
// verification fails -- not used for anything security-relevant, and this
// client's trust decision is still made entirely by mbedTLS's own chain
// verification against g_ca_chain, not by anything read from this buffer.
static char g_last_chain_dump[400];

static int ota_tls_verify_cb(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)data;
    char subj[80] = {0};
    char iss[80] = {0};
    mbedtls_x509_dn_gets(subj, sizeof(subj), &crt->subject);
    mbedtls_x509_dn_gets(iss, sizeof(iss), &crt->issuer);
    ota_tls_sanitize_for_json(subj);
    ota_tls_sanitize_for_json(iss);

    size_t off = strlen(g_last_chain_dump);
    if (off + 16 < sizeof(g_last_chain_dump)) {
        int n = snprintf(g_last_chain_dump + off, sizeof(g_last_chain_dump) - off,
                          "%s[%d] subject=%s issuer=%s flags=0x%08x",
                          off > 0 ? " | " : "", depth, subj, iss, (unsigned int)*flags);
        (void)n;
    }
    return 0;  // don't influence the verification result -- purely observing it
}

bool ota_tls_global_init(char *err, size_t err_len) {
    if (g_global_ready) {
        return true;
    }

    ota_debug_checkpoint(OTA_DEBUG_CP_TLS_GLOBAL_INIT);

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr_drbg);
    mbedtls_x509_crt_init(&g_ca_chain);

    static const char pers[] = "opentrickler-ota-client";
    int ret = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy,
                                     (const unsigned char *)pers, sizeof(pers) - 1);
    if (ret != 0) {
        snprintf(err, err_len, "RNG seed failed (-0x%04x)", (unsigned int)-ret);
        return false;
    }

    // Diagnostic: free heap immediately either side of the bulk parse below.
    // mbedtls_x509_crt_parse() calloc()s a handful of small buffers per
    // certificate (raw DER copy, PK context, name entries, and -- for CA
    // certs with more than one Certificate Policies OID, like several of
    // these roots/intermediates -- an extra mbedtls_asn1_sequence node per
    // extra policy). If free heap is already tight by the time this runs
    // (it runs on-demand, well after WiFi/lwIP/UI tasks are all up, not at
    // cold boot), one of those small allocations failing mid-certificate is
    // reported back as a plain per-certificate parse failure with no way to
    // tell it apart from a genuinely malformed certificate -- hence
    // capturing heap here rather than guessing from the symptom alone.
    size_t heap_before = xPortGetFreeHeapSize();

    // ota_tls_ca_roots_pem is several concatenated PEM certs; mbedtls parses
    // as many as it can and returns the count of ones it *couldn't* parse.
    // A handful of roots is small enough that we treat any successful parse
    // (ret >= 0, i.e. no hard parse error) as good enough to proceed.
    ret = mbedtls_x509_crt_parse(&g_ca_chain,
                                  (const unsigned char *)ota_tls_ca_roots_pem,
                                  strlen(ota_tls_ca_roots_pem) + 1);
    size_t heap_after = xPortGetFreeHeapSize();
    if (ret < 0) {
        snprintf(err, err_len, "CA bundle parse failed (-0x%04x)", (unsigned int)-ret);
        return false;
    }

    // Diagnostic: record exactly what ended up loaded, not just whether
    // parsing returned an error -- ret > 0 here means *some* certs in the
    // bundle failed to parse individually while the rest still loaded, and
    // that case was previously silent. See ota_tls_ca_chain_summary().
    {
        int loaded = 0;
        size_t off = (size_t)snprintf(g_ca_chain_summary, sizeof(g_ca_chain_summary),
                                       "%d failed to parse (heap before=%u after=%u bytes); loaded: ",
                                       ret, (unsigned)heap_before, (unsigned)heap_after);
        for (mbedtls_x509_crt *c = &g_ca_chain; c != NULL; c = c->next) {
            char cn[64] = {0};
            mbedtls_x509_dn_gets(cn, sizeof(cn), &c->subject);
            ota_tls_sanitize_for_json(cn);
            loaded++;
            if (off + 4 < sizeof(g_ca_chain_summary)) {
                int n = snprintf(g_ca_chain_summary + off, sizeof(g_ca_chain_summary) - off,
                                  "%s%s", loaded > 1 ? ", " : "", cn);
                if (n > 0) {
                    off += (size_t)n;
                }
            }
        }
    }

    g_global_ready = true;
    ota_debug_checkpoint(OTA_DEBUG_CP_TLS_GLOBAL_INIT_OK);
    return true;
}

static bool ota_tls_resolve(const char *host, struct in_addr *out_addr) {
    if (inet_aton(host, out_addr) != 0) {
        return true;
    }
    struct hostent *he = lwip_gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL) {
        return false;
    }
    memcpy(out_addr, he->h_addr_list[0], sizeof(struct in_addr));
    return true;
}

// Both sockets used here are blocking, bounded only by SO_RCVTIMEO/SNDTIMEO
// (set in ota_tls_connect()) -- never O_NONBLOCK -- so a negative return
// from lwip_send/lwip_recv always means a real failure (including our own
// configured timeout expiring), never "try again". That keeps this BIO
// simple: it never has to hand mbedtls a WANT_READ/WANT_WRITE retry signal.
static int ota_tls_bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int sock = *(int *)ctx;
    int n = lwip_send(sock, buf, len, 0);
    if (n < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int ota_tls_bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int sock = *(int *)ctx;
    int n = lwip_recv(sock, buf, len, 0);
    if (n < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return n;  // 0 == peer closed the connection; mbedtls_ssl_read() surfaces this as a clean EOF (return 0)
}

ota_tls_conn_t *ota_tls_connect(const char *host, uint16_t port, char *err, size_t err_len) {
    if (!ota_tls_global_init(err, err_len)) {
        return NULL;
    }

    ota_debug_checkpoint(OTA_DEBUG_CP_DNS_RESOLVE);
    struct in_addr addr;
    if (!ota_tls_resolve(host, &addr)) {
        snprintf(err, err_len, "could not resolve host \"%s\"", host);
        return NULL;
    }

    ota_debug_checkpoint(OTA_DEBUG_CP_TCP_CONNECT);
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(err, err_len, "socket() failed");
        return NULL;
    }

    struct timeval tv;
    tv.tv_sec = 12;
    tv.tv_usec = 0;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = lwip_htons(port);
    server_addr.sin_addr = addr;

    if (lwip_connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        snprintf(err, err_len, "could not connect to %s:%u", host, (unsigned)port);
        lwip_close(sock);
        return NULL;
    }
    ota_debug_checkpoint(OTA_DEBUG_CP_TCP_CONNECTED);

    ota_tls_conn_t *conn = pvPortMalloc(sizeof(ota_tls_conn_t));
    if (conn == NULL) {
        snprintf(err, err_len, "out of memory");
        lwip_close(sock);
        return NULL;
    }
    memset(conn, 0, sizeof(*conn));
    conn->sock = sock;

    ota_debug_checkpoint(OTA_DEBUG_CP_SSL_SETUP);
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->conf);

    int ret = mbedtls_ssl_config_defaults(&conn->conf, MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        snprintf(err, err_len, "ssl_config_defaults failed (-0x%04x)", (unsigned int)-ret);
        goto fail;
    }

    mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conn->conf, &g_ca_chain, NULL);
    mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &g_ctr_drbg);
    g_last_chain_dump[0] = '\0';
    mbedtls_ssl_conf_verify(&conn->conf, ota_tls_verify_cb, NULL);

    ret = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
    if (ret != 0) {
        snprintf(err, err_len, "ssl_setup failed (-0x%04x)", (unsigned int)-ret);
        goto fail;
    }

    ret = mbedtls_ssl_set_hostname(&conn->ssl, host);
    if (ret != 0) {
        snprintf(err, err_len, "ssl_set_hostname failed (-0x%04x)", (unsigned int)-ret);
        goto fail;
    }

    mbedtls_ssl_set_bio(&conn->ssl, &conn->sock, ota_tls_bio_send, ota_tls_bio_recv, NULL);

    ota_debug_checkpoint(OTA_DEBUG_CP_TLS_HANDSHAKE);
    ret = mbedtls_ssl_handshake(&conn->ssl);
    if (ret != 0) {
        if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            // The handshake aborts with this generic code whenever
            // certificate verification fails, but mbedtls_ssl_get_verify_result()
            // still has the specific reason (untrusted, expired, hostname
            // mismatch, ...) even though the handshake itself didn't
            // complete -- surface that instead of just the generic code.
            uint32_t verify_flags = mbedtls_ssl_get_verify_result(&conn->ssl);
            char vbuf[160];
            mbedtls_x509_crt_verify_info(vbuf, sizeof(vbuf), "", verify_flags);
            size_t vlen = strlen(vbuf);
            while (vlen > 0 && (vbuf[vlen - 1] == '\n' || vbuf[vlen - 1] == '\r')) {
                vbuf[--vlen] = '\0';
            }
            int used = snprintf(err, err_len, "certificate for %s did not verify (flags 0x%08x): %s",
                                 host, (unsigned int)verify_flags, vlen > 0 ? vbuf : "(no detail available)");

            // Ground truth: append exactly what chain the server presented,
            // captured by ota_tls_verify_cb() as mbedTLS walked it (see that
            // function) -- gathered *during* verification, so it survives
            // even though mbedTLS tears down session_negotiate once the
            // handshake aborts on failure.
            if (used > 0 && (size_t)used < err_len && g_last_chain_dump[0] != '\0') {
                snprintf(err + used, err_len - (size_t)used, " | %s", g_last_chain_dump);
            }
        }
        else {
            snprintf(err, err_len, "TLS handshake with %s failed (-0x%04x)", host, (unsigned int)-ret);
        }
        goto fail;
    }
    ota_debug_checkpoint(OTA_DEBUG_CP_TLS_HANDSHAKE_OK);

    uint32_t verify_flags = mbedtls_ssl_get_verify_result(&conn->ssl);
    if (verify_flags != 0) {
        char vbuf[160];
        mbedtls_x509_crt_verify_info(vbuf, sizeof(vbuf), "", verify_flags);
        // strip the trailing newline mbedtls adds
        size_t vlen = strlen(vbuf);
        while (vlen > 0 && (vbuf[vlen - 1] == '\n' || vbuf[vlen - 1] == '\r')) {
            vbuf[--vlen] = '\0';
        }
        snprintf(err, err_len, "certificate for %s did not verify: %s", host, vbuf);
        goto fail;
    }
    ota_debug_checkpoint(OTA_DEBUG_CP_CERT_VERIFIED);

    return conn;

fail:
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    lwip_close(sock);
    vPortFree(conn);
    return NULL;
}

int ota_tls_read(ota_tls_conn_t *conn, uint8_t *buf, size_t len) {
    if (conn == NULL) {
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    }
    return mbedtls_ssl_read(&conn->ssl, buf, len);
}

int ota_tls_write(ota_tls_conn_t *conn, const uint8_t *buf, size_t len) {
    if (conn == NULL) {
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    }
    size_t sent = 0;
    while (sent < len) {
        int n = mbedtls_ssl_write(&conn->ssl, buf + sent, len - sent);
        if (n < 0) {
            return n;
        }
        if (n == 0) {
            break;
        }
        sent += (size_t)n;
    }
    return (int)sent;
}

void ota_tls_close(ota_tls_conn_t *conn) {
    if (conn == NULL) {
        return;
    }
    mbedtls_ssl_close_notify(&conn->ssl);
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    lwip_close(conn->sock);
    vPortFree(conn);
}
