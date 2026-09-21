// Auto-extracted trust anchors for ota_client.c's TLS connections to
// github.com / *.githubusercontent.com (and whatever CDN a release-asset
// redirect points at). This is a small, curated subset of the public Mozilla
// CA root store (extracted 2026-09-21 via Python's `certifi` package, which
// tracks Mozilla's official release), not a pin to any single CA -- picking
// several long-lived, widely-cross-signed roots (Let's Encrypt, DigiCert,
// Sectigo/USERTrust, GlobalSign, Amazon, Google Trust Services) means this
// keeps working even if GitHub or its CDN switches which CA it uses, without
// needing every root in the ~230KB full Mozilla bundle (which would cost
// much more flash and, more importantly, RAM to parse than an embedded
// device needs for one outbound purpose).
//
// To regenerate: pip install certifi, then extract the PEM blocks for the
// "Label:" values below from `python3 -c "import certifi;print(certifi.where())"`
// and re-run the same extraction as this file's header comment describes.
// Labels included: ISRG Root X1, ISRG Root X2, DigiCert Global Root G2,
// DigiCert Global Root G3, DigiCert TLS ECC P384 Root G5, DigiCert TLS
// RSA4096 Root G5, USERTrust RSA Certification Authority, USERTrust ECC
// Certification Authority, GlobalSign Root R46, GlobalSign ECC Root CA - R5,
// Amazon Root CA 1, Amazon Root CA 3, GTS Root R1, GTS Root R4.

#ifndef OTA_TLS_ROOTS_H_
#define OTA_TLS_ROOTS_H_

#ifdef __cplusplus
extern "C" {
#endif

// PEM-encoded, concatenated. mbedtls_x509_crt_parse() wants the length
// including the terminating NUL for PEM input -- use sizeof(...), not
// strlen()+1 is also fine, but sizeof is simplest at the call site.
extern const char ota_tls_ca_roots_pem[];

#ifdef __cplusplus
}
#endif

#endif  // OTA_TLS_ROOTS_H_
