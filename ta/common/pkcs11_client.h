#ifndef FORTRESS_PKCS11_CLIENT_H
#define FORTRESS_PKCS11_CLIENT_H

#include <stdint.h>
#include <tee_internal_api.h>

#define FORTRESS_P11_SOA "fortress-soa"
#define FORTRESS_P11_ROOT "fortress-root"
#define FORTRESS_P11_IMAGE "fortress-image"

/* 1 when the AES object is already in the token. 0 when it is absent or the token is down. */
int fortress_pkcs11_has_aes(void);

/* Replace the AES-128 object. The 16 bytes are not kept in this TA. */
TEE_Result fortress_pkcs11_import_aes(const uint8_t key[16]);

/* Replace one P-256 public key. point is the 65-byte uncompressed point. */
TEE_Result fortress_pkcs11_import_p256(const char *label, const uint8_t point[65]);

/*
 * AES-128-GCM. IV is 12 bytes, aad is 10 bytes, tag is 16 bytes.
 * Encrypt writes ciphertext to out and the tag to tag.
 * Decrypt reads ciphertext from in and the tag from tag.
 */
TEE_Result fortress_pkcs11_aes_gcm(int encrypt, const uint8_t iv[12], const uint8_t aad[10],
				   const uint8_t *in, uint32_t in_len, uint8_t *out,
				   uint8_t tag[16]);

/* Verify a SHA-256 digest with CKM_ECDSA. The token does not hash again. */
TEE_Result fortress_pkcs11_verify_p256(const char *label, const uint8_t digest[32],
				       const uint8_t sig[64]);

#endif
