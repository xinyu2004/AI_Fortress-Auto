#ifndef FORTRESS_CRYPTO_H
#define FORTRESS_CRYPTO_H

#include "fortress/trust.h"

#include <stddef.h>
#include <stdint.h>

int fortress_sha256(const uint8_t *data, size_t len, uint8_t out[FORTRESS_HASH_LEN]);
int fortress_random(uint8_t *buf, size_t len);
int fortress_p256_generate(const char *pem_path, uint8_t pub[FORTRESS_PK_LEN]);
int fortress_p256_read_pub(const char *pem_path, uint8_t pub[FORTRESS_PK_LEN]);
int fortress_p256_sign(const char *pem_path, const uint8_t *msg, size_t msg_len,
		       uint8_t sig[FORTRESS_SIG_LEN]);
int fortress_p256_verify(const uint8_t pk[FORTRESS_PK_LEN],
			 const uint8_t *msg, size_t msg_len,
			 const uint8_t sig[FORTRESS_SIG_LEN]);

#endif
