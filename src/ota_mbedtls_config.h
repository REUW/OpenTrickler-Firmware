#ifndef OTA_MBEDTLS_CONFIG_H_
#define OTA_MBEDTLS_CONFIG_H_

// Minimal mbedTLS 3.x configuration for ota_client.c's one job: act as a
// TLS 1.2 client to fetch a couple of small files from github.com /
// *.githubusercontent.com (or whatever CDN a release-asset redirect points
// at) over HTTPS, verifying the server's certificate chain against the
// curated root bundle in ota_tls_roots.c. This is NOT a general-purpose
// mbedtls config: no TLS server support, no DTLS, no TLS 1.3 (1.2 with
// ECDHE + AES-GCM/CBC covers essentially every server we'll talk to and
// keeps the client's code path -- and its RAM footprint -- much smaller and
// easier to reason about than pulling in both protocol versions). Picked to
// stay comfortably inside this firmware's RAM budget: the biggest cost is
// the SSL in/out record buffers below (~18.5KB together, allocated only
// while an update check or download is actually in flight).
//
// PICO_MBEDTLS_CONFIG_FILE is set to this file's name in CMakeLists.txt.

// --- Platform / build ---
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ERROR_C
#define MBEDTLS_VERSION_C

// --- Entropy / RNG. mbedtls_hardware_poll() is provided by the SDK's own
// pico_mbedtls.c (linked in via the pico_mbedtls CMake target) and reads
// the RP2350's hardware RNG through pico/rand.h -- MBEDTLS_ENTROPY_HARDWARE_ALT
// is what tells mbedtls's entropy.c to actually call it. ---
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_CTR_DRBG_C

// --- TLS 1.2 client only ---
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
// Without this, mbedTLS keeps only a digest of the peer's certificate after
// verification, not the certificate itself, so mbedtls_ssl_get_peer_cert()
// returns NULL. ota_tls.c uses it to report exactly which certificate chain
// the server presented when verification fails -- essential for diagnosing
// a real device against a real server, where we can't just log in and look.
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE

// Keep the handshake's incoming buffer generous -- GitHub/its CDN may send
// a multi-certificate chain in one handshake flight and we can't rely on
// every server honoring a max_fragment_length request -- while keeping the
// outgoing buffer small, since everything we ever send (ClientHello, a
// short HTTP GET) is tiny. This asymmetry is the main RAM lever here.
#define MBEDTLS_SSL_IN_CONTENT_LEN      16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN     2048
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH

// --- Key exchange / ciphers covering the RSA and ECDSA cert chains in
// common use across GitHub's infrastructure and CDNs (see ota_tls_roots.h
// for why several CA families are trusted rather than one) ---
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED

#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CCM_C
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CBC

#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
// SHA-384 is a genuinely separate config option from SHA-512 in this
// mbedTLS version (both live in sha512.c, but md.c only wires up SHA-384
// support when MBEDTLS_SHA384_C is defined) -- without it, mbedTLS's OID
// lookup can't map the sha384WithRSAEncryption / ecdsa-with-SHA384 OIDs to
// a hash algorithm at all, and mbedtls_x509_crt_parse() fails any
// certificate signed that way with MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG ("OID
// is not found") -- not a verification failure, a parse failure, so the
// certificate never even makes it into the trust store. This was confirmed
// by parsing this file's whole CA bundle certificate-by-certificate with a
// host build of this exact vendored mbedTLS + this exact config: 13 of the
// 17 embedded certs -- effectively every non-2016-era one, since SHA-384
// is the modern default for higher-strength CA signatures -- failed with
// exactly that error until this define was added, matching this project's
// own on-device diagnostic (ca_bundle_summary in /rest/update_status)
// showing only 4 of 17 roots ever actually loaded.
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SHA1_C

#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_OID_C
#define MBEDTLS_BIGNUM_C

#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED

// --- X.509 cert chain parsing/verification ---
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

#endif  // OTA_MBEDTLS_CONFIG_H_
