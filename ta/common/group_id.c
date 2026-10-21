#include "group_id.h"

#include <stdio.h>
#include <string.h>
#include <tee_internal_api.h>

/* Same bytes as Linux UUID_INIT(0x58ac9ca0, 0x2086, 0x4683, 0xa1, ...). */
static const uint8_t kernel_ns[16] = {
	0x58, 0xac, 0x9c, 0xa0, 0x20, 0x86, 0x46, 0x83,
	0xa1, 0xb8, 0xec, 0x4b, 0xc0, 0x8e, 0x01, 0xb6
};

static TEE_Result sha1_2(const void *a, size_t a_len, const void *b, size_t b_len,
			 uint8_t out[20])
{
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	size_t sz = 20;
	TEE_Result rc;

	rc = TEE_AllocateOperation(&op, TEE_ALG_SHA1, TEE_MODE_DIGEST, 0);
	if (rc != TEE_SUCCESS)
		return rc;
	TEE_DigestUpdate(op, a, a_len);
	rc = TEE_DigestDoFinal(op, b, b_len, out, &sz);
	TEE_FreeOperation(op);
	return rc;
}

TEE_Result fortress_group_uuid(uint32_t gid, TEE_UUID *out)
{
	char name[16];
	uint8_t hash[20];
	int n;
	TEE_Result rc;

	n = snprintf(name, sizeof(name), "gid=%x", gid);
	if (n < 0 || n >= (int)sizeof(name))
		return TEE_ERROR_BAD_PARAMETERS;
	rc = sha1_2(kernel_ns, sizeof(kernel_ns), name, (size_t)n, hash);
	if (rc != TEE_SUCCESS)
		return rc;
	hash[6] = (uint8_t)((hash[6] & 0x0f) | 0x50);
	hash[8] = (uint8_t)((hash[8] & 0x3f) | 0x80);
	out->timeLow = ((uint32_t)hash[0] << 24) | ((uint32_t)hash[1] << 16) |
		       ((uint32_t)hash[2] << 8) | hash[3];
	out->timeMid = (uint16_t)(((uint16_t)hash[4] << 8) | hash[5]);
	out->timeHiAndVersion = (uint16_t)(((uint16_t)hash[6] << 8) | hash[7]);
	TEE_MemMove(out->clockSeqAndNode, hash + 8, 8);
	return TEE_SUCCESS;
}

int fortress_client_is_gid(uint32_t gid)
{
	TEE_Identity id;
	TEE_UUID expect;
	TEE_Result rc;

	rc = TEE_GetPropertyAsIdentity(TEE_PROPSET_CURRENT_CLIENT,
				       "gpd.client.identity", &id);
	if (rc != TEE_SUCCESS || id.login != TEE_LOGIN_GROUP)
		return 0;
	if (fortress_group_uuid(gid, &expect) != TEE_SUCCESS)
		return 0;
	return TEE_MemCompare(&id.uuid, &expect, sizeof(expect)) == 0;
}
