#ifndef FORTRESS_TRUST_H
#define FORTRESS_TRUST_H

#include <stddef.h>
#include <stdint.h>

#define FORTRESS_PK_LEN 65
#define FORTRESS_SIG_LEN 64
#define FORTRESS_HASH_LEN 32
#define FORTRESS_MAGIC_FKM1 0x314D4B46u /* 'FKM1' little-endian */

enum {
	FORTRESS_MANIFEST_BODY = 85,
	FORTRESS_MANIFEST_LEN = 149
};

typedef struct fortress_trust {
	uint16_t ecu_id;
	uint32_t kvn;
	uint32_t min_svn;
	uint8_t root_pk[FORTRESS_PK_LEN];
	uint8_t image_pk[FORTRESS_PK_LEN];
	uint8_t root_hash[FORTRESS_HASH_LEN];
} fortress_trust;

typedef struct fortress_otp {
	uint8_t rotpk_hash[FORTRESS_HASH_LEN];
	int closed;
} fortress_otp;

int fortress_trust_provision(fortress_trust *trust, uint16_t ecu_id,
			     const uint8_t root_pk[FORTRESS_PK_LEN],
			     const uint8_t image_pk[FORTRESS_PK_LEN],
			     uint32_t kvn, uint32_t min_svn);

/* not_before/not_after 都为 0 表示不限制有效期。签名覆盖签名字段之前的全部字节。 */
int fortress_manifest_pack(uint16_t ecu_id, uint32_t key_version,
			   uint32_t not_before, uint32_t not_after,
			   const uint8_t image_pk[FORTRESS_PK_LEN],
			   const char *root_pem,
			   uint8_t out[FORTRESS_MANIFEST_LEN]);

int fortress_manifest_apply(fortress_trust *trust,
			    const uint8_t manifest[FORTRESS_MANIFEST_LEN],
			    uint32_t now);

int fortress_otp_provision(fortress_otp *otp, const uint8_t hash[FORTRESS_HASH_LEN]);
int fortress_otp_lock(fortress_otp *otp);

#endif
