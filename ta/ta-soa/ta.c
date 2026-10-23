#include "group_id.h"
#include "pkcs11_client.h"
#include "wire.h"

#include <fortress/ta_ids.h>

#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#define FRESH_SLOTS 16
#define FRESH_WINDOW 8u

struct fresh_slot {
	uint16_t service_id;
	uint32_t tx;
	uint32_t rx;
	int present;
};

static struct fresh_slot fresh[FRESH_SLOTS];
static int key_ready;

static struct fresh_slot *slot_for(uint16_t service_id, int create)
{
	int i;
	int free_slot = -1;

	for (i = 0; i < FRESH_SLOTS; i++) {
		if (fresh[i].present && fresh[i].service_id == service_id)
			return &fresh[i];
		if (!fresh[i].present && free_slot < 0)
			free_slot = i;
	}
	if (!create || free_slot < 0)
		return NULL;
	fresh[free_slot].service_id = service_id;
	fresh[free_slot].tx = 0;
	fresh[free_slot].rx = 0;
	fresh[free_slot].present = 1;
	return &fresh[free_slot];
}

static void iv_from(uint16_t service_id, uint16_t method_id, uint32_t freshness, uint8_t iv[12])
{
	TEE_MemFill(iv, 0, 12);
	iv[0] = (uint8_t)service_id;
	iv[1] = (uint8_t)(service_id >> 8);
	iv[2] = (uint8_t)method_id;
	iv[3] = (uint8_t)(method_id >> 8);
	iv[4] = (uint8_t)freshness;
	iv[5] = (uint8_t)(freshness >> 8);
	iv[6] = (uint8_t)(freshness >> 16);
	iv[7] = (uint8_t)(freshness >> 24);
}

static TEE_Result gcm_crypt(int encrypt, const uint8_t iv[12], const uint8_t *aad,
			    const uint8_t *in, uint32_t in_len, uint8_t *out, uint8_t tag[16])
{
	return fortress_pkcs11_aes_gcm(encrypt, iv, aad, in, in_len, out, tag);
}

static TEE_Result cmd_install(uint32_t pt, TEE_Param params[4])
{
	uint8_t key[16];
	TEE_Result rc;

	if (!fortress_client_is_gid(FORTRESS_GID_OTA))
		return TEE_ERROR_ACCESS_DENIED;
	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[0].memref.size != sizeof(key))
		return TEE_ERROR_BAD_PARAMETERS;
	TEE_MemMove(key, params[0].memref.buffer, sizeof(key));
	rc = fortress_pkcs11_import_aes(key);
	TEE_MemFill(key, 0, sizeof(key));
	if (rc != TEE_SUCCESS)
		return rc;
	key_ready = 1;
	TEE_MemFill(fresh, 0, sizeof(fresh));
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_seal(uint32_t pt, TEE_Param params[4])
{
	const uint8_t *in;
	uint16_t sid;
	uint16_t mid;
	uint16_t plain_len;
	struct fresh_slot *slot;
	uint32_t next;
	uint8_t iv[12];
	uint8_t *out;
	uint32_t need;
	TEE_Result rc;

	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_INPUT ||
	    TEE_PARAM_TYPE_GET(pt, 1) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	if (!key_ready)
		return TEE_ERROR_BAD_STATE;
	in = params[0].memref.buffer;
	if (params[0].memref.size < 6)
		return TEE_ERROR_BAD_PARAMETERS;
	sid = (uint16_t)in[0] | ((uint16_t)in[1] << 8);
	mid = (uint16_t)in[2] | ((uint16_t)in[3] << 8);
	plain_len = (uint16_t)in[4] | ((uint16_t)in[5] << 8);
	if (params[0].memref.size != (uint32_t)6 + plain_len)
		return TEE_ERROR_BAD_PARAMETERS;
	if (sid == FORTRESS_SID_PERCEPTION) {
		if (!fortress_client_is_gid(FORTRESS_GID_PERCEPTION))
			return TEE_ERROR_ACCESS_DENIED;
	} else if (sid == FORTRESS_SID_PLANNING) {
		if (!fortress_client_is_gid(FORTRESS_GID_PLANNING))
			return TEE_ERROR_ACCESS_DENIED;
	} else {
		return TEE_ERROR_BAD_PARAMETERS;
	}
	slot = slot_for(sid, 1);
	if (!slot)
		return TEE_ERROR_STORAGE_NO_SPACE;
	next = slot->tx + 1u;
	need = 10u + plain_len + 16u;
	if (params[1].memref.size < need)
		return TEE_ERROR_SHORT_BUFFER;
	out = params[1].memref.buffer;
	out[0] = (uint8_t)sid;
	out[1] = (uint8_t)(sid >> 8);
	out[2] = (uint8_t)mid;
	out[3] = (uint8_t)(mid >> 8);
	out[4] = (uint8_t)next;
	out[5] = (uint8_t)(next >> 8);
	out[6] = (uint8_t)(next >> 16);
	out[7] = (uint8_t)(next >> 24);
	out[8] = (uint8_t)plain_len;
	out[9] = (uint8_t)(plain_len >> 8);
	iv_from(sid, mid, next, iv);
	rc = gcm_crypt(1, iv, out, in + 6, plain_len, out + 10, out + 10 + plain_len);
	if (rc != TEE_SUCCESS) {
		fortress_log_gap();
		return rc;
	}
	slot->tx = next;
	params[1].memref.size = need;
	IMSG("sealed service %u freshness %u", sid, next);
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_open(uint32_t pt, TEE_Param params[4])
{
	const uint8_t *frame;
	uint32_t frame_len;
	uint16_t sid;
	uint16_t mid;
	uint16_t ct_len;
	uint32_t freshness;
	struct fresh_slot *slot;
	uint8_t iv[12];
	uint8_t *out;
	TEE_Result rc;

	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_INPUT ||
	    TEE_PARAM_TYPE_GET(pt, 1) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	if (!key_ready)
		return TEE_ERROR_BAD_STATE;
	frame = params[0].memref.buffer;
	frame_len = params[0].memref.size;
	if (frame_len < 10u + 16u)
		return TEE_ERROR_BAD_PARAMETERS;
	sid = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
	mid = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
	freshness = (uint32_t)frame[4] | ((uint32_t)frame[5] << 8) |
		    ((uint32_t)frame[6] << 16) | ((uint32_t)frame[7] << 24);
	ct_len = (uint16_t)frame[8] | ((uint16_t)frame[9] << 8);
	if (frame_len != 10u + ct_len + 16u)
		return TEE_ERROR_BAD_PARAMETERS;
	if (sid == FORTRESS_SID_PERCEPTION) {
		if (!fortress_client_is_gid(FORTRESS_GID_PLANNING))
			return TEE_ERROR_ACCESS_DENIED;
	} else if (sid == FORTRESS_SID_PLANNING) {
		if (!fortress_client_is_gid(FORTRESS_GID_PERCEPTION))
			return TEE_ERROR_ACCESS_DENIED;
	} else {
		return TEE_ERROR_BAD_PARAMETERS;
	}
	slot = slot_for(sid, 1);
	if (!slot)
		return TEE_ERROR_STORAGE_NO_SPACE;
	if (freshness == 0) {
		IMSG("rejected replay service %u freshness 0", sid);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	if (slot->rx == 0) {
		if (freshness > FRESH_WINDOW)
			return TEE_ERROR_SECURITY;
	} else if (freshness <= slot->rx) {
		IMSG("rejected replay service %u freshness %u", sid, freshness);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	} else if (freshness > slot->rx + FRESH_WINDOW) {
		return TEE_ERROR_SECURITY;
	}
	if (params[1].memref.size < 6u + ct_len)
		return TEE_ERROR_SHORT_BUFFER;
	out = params[1].memref.buffer;
	iv_from(sid, mid, freshness, iv);
	rc = gcm_crypt(0, iv, frame, frame + 10, ct_len, out + 6, (uint8_t *)frame + 10 + ct_len);
	if (rc != TEE_SUCCESS) {
		IMSG("rejected tag service %u", sid);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	slot->rx = freshness;
	out[0] = (uint8_t)sid;
	out[1] = (uint8_t)(sid >> 8);
	out[2] = (uint8_t)mid;
	out[3] = (uint8_t)(mid >> 8);
	out[4] = (uint8_t)ct_len;
	out[5] = (uint8_t)(ct_len >> 8);
	params[1].memref.size = 6u + ct_len;
	IMSG("opened service %u freshness %u", sid, freshness);
	fortress_log_gap();
	return TEE_SUCCESS;
}

TEE_Result TA_CreateEntryPoint(void)
{
	if (fortress_pkcs11_has_aes())
		key_ready = 1;
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t pt, TEE_Param params[4], void **sess)
{
	(void)pt;
	(void)params;
	(void)sess;
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess)
{
	(void)sess;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess, uint32_t cmd, uint32_t pt,
				      TEE_Param params[4])
{
	(void)sess;
	switch (cmd) {
	case CMD_SOA_INSTALL_KEY:
		return cmd_install(pt, params);
	case CMD_SOA_SEAL:
		return cmd_seal(pt, params);
	case CMD_SOA_OPEN:
		return cmd_open(pt, params);
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
