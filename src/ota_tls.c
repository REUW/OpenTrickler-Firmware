#include "ota_tls.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"

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

struct ota_tls_conn {
    int sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
};

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr_drbg;
static mbedtls_x509_crt g_ca_chain;
static bool g_global_ready = false;

bool ota_tls_global_init(char *err, size_t err_len) {
    if (g_global_ready) {
        return true;
    }

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

    // ota_tls_ca_roots_pem is several concatenated PEM certs; mbedtls parses
    // as many as it can and returns the count of ones it *couldn't* parse.
    // A handful of roots is small enough that we treat any successful parse
    // (ret >= 0, i.e. no hard parse error) as good enough to proceed.
    ret = mbedtls_x509_crt_parse(&g_ca_chain,
                                  (const unsigned char *)ota_tls_ca_roots_pem,
                                  strlen(ota_tls_ca_roots_pem) + 1);
    if (ret < 0) {
        snprintf(err, err_len, "CA bundle parse failed (-0x%04x)", (unsigned int)-ret);
        return false;
    }

    g_global_ready = true;
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

    struct in_addr addr;
    if (!ota_tls_resolve(host, &addr)) {
        snprintf(err, err_len, "could not resolve host \"%s\"", host);
        return NULL;
    }

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

    ota_tls_conn_t *conn = pvPortMalloc(sizeof(ota_tls_conn_t));
    if (conn == NULL) {
        snprintf(err, err_len, "out of memory");
        lwip_close(sock);
        return NULL;
    }
    memset(conn, 0, sizeof(*conn));
    conn->sock = sock;

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

    ret = mbedtls_ssl_handshake(&conn->ssl);
    if (ret != 0) {
        snprintf(err, err_len, "TLS handshake with %s failed (-0x%04x)", host, (unsigned int)-ret);
        goto fail;
    }

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
