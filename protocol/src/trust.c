#include "fortress/trust.h"

#include "fortress/codec.h"
#include "fortress/crypto.h"
#include "fortress/status.h"

#include <string.h>

int fortress_trust_provision(fortress_trust *trust, uint16_t ecu_id,
			     const uint8_t root_pk[FORTRESS_PK_LEN],
			     const uint8_t image_pk[FORTRESS_PK_LEN],
			     uint32_t kvn, uint32_t min_svn)
{
	int rc;

	if (!trust || !root_pk || !image_pk || kvn == 0)
		return FORTRESS_ERR_ARG;
	memset(trust, 0, sizeof(*trust));
	trust->ecu_id = ecu_id;
	trust->kvn = kvn;
	trust->min_svn = min_svn;
	memcpy(trust->root_pk, root_pk, FORTRESS_PK_LEN);
	memcpy(trust->image_pk, image_pk, FORTRESS_PK_LEN);
	rc = fortress_sha256(root_pk, FORTRESS_PK_LEN, trust->root_hash);
	return rc;
}

int fortress_manifest_pack(uint16_t ecu_id, uint32_t key_version,
			   uint32_t not_before, uint32_t not_after,
			   const uint8_t image_pk[FORTRESS_PK_LEN],
			   const char *root_pem,
			   uint8_t out[FORTRESS_MANIFEST_LEN])
{
	int rc;

	if (!image_pk || !root_pem || !out || key_version == 0)
		return FORTRESS_ERR_ARG;
	memset(out, 0, FORTRESS_MANIFEST_LEN);
	fortress_put_u32(out, FORTRESS_MAGIC_FKM1);
	fortress_put_u16(out + 4, 1);
	fortress_put_u16(out + 6, ecu_id);
	fortress_put_u32(out + 8, key_version);
	fortress_put_u32(out + 12, not_before);
	fortress_put_u32(out + 16, not_after);
	memcpy(out + 20, image_pk, FORTRESS_PK_LEN);
	rc = fortress_p256_sign(root_pem, out, FORTRESS_MANIFEST_BODY,
				out + FORTRESS_MANIFEST_BODY);
	return rc;
}

int fortress_manifest_apply(fortress_trust *trust,
			    const uint8_t manifest[FORTRESS_MANIFEST_LEN],
			    uint32_t now)
{
	uint8_t root_hash[FORTRESS_HASH_LEN];
	uint32_t not_before;
	uint32_t not_after;
	uint32_t kvn;
	int rc;

	if (!trust || !manifest)
		return FORTRESS_ERR_ARG;
	if (fortress_get_u32(manifest) != FORTRESS_MAGIC_FKM1)
		return FORTRESS_ERR_MAGIC;
	if (fortress_get_u16(manifest + 6) != trust->ecu_id)
		return FORTRESS_ERR_ARG;
	rc = fortress_sha256(trust->root_pk, FORTRESS_PK_LEN, root_hash);
	if (rc != FORTRESS_OK)
		return rc;
	if (memcmp(root_hash, trust->root_hash, FORTRESS_HASH_LEN) != 0)
		return FORTRESS_ERR_SIGNATURE;
	rc = fortress_p256_verify(trust->root_pk, manifest, FORTRESS_MANIFEST_BODY,
				  manifest + FORTRESS_MANIFEST_BODY);
	if (rc != FORTRESS_OK)
		return rc;
	not_before = fortress_get_u32(manifest + 12);
	not_after = fortress_get_u32(manifest + 16);
	if ((not_before || not_after) && (now < not_before || now > not_after))
		return FORTRESS_ERR_STATE;
	kvn = fortress_get_u32(manifest + 8);
	if (kvn <= trust->kvn)
		return FORTRESS_ERR_KVN;
	memcpy(trust->image_pk, manifest + 20, FORTRESS_PK_LEN);
	trust->kvn = kvn;
	return FORTRESS_OK;
}

int fortress_otp_provision(fortress_otp *otp, const uint8_t hash[FORTRESS_HASH_LEN])
{
	if (!otp || !hash)
		return FORTRESS_ERR_ARG;
	if (otp->closed)
		return FORTRESS_ERR_LOCKED;
	memcpy(otp->rotpk_hash, hash, FORTRESS_HASH_LEN);
	return FORTRESS_OK;
}

int fortress_otp_lock(fortress_otp *otp)
{
	if (!otp)
		return FORTRESS_ERR_ARG;
	otp->closed = 1;
	return FORTRESS_OK;
}
