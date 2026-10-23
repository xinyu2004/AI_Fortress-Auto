#include "group_id.h"
#include "pkcs11_client.h"
#include "trust_init.h"
#include "wire.h"

#include <fortress/image.h>
#include <fortress/ta_ids.h>
#include <fortress/trust.h>

#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#define STATE_ID "fortress.ota"
#define CHUNK_MAX 8
#define SEED_LEN 32

struct ota_sess {
	uint8_t seed[SEED_LEN];
	int have_seed;
	int unlocked;
};

struct trust_disk {
	uint32_t kvn;
	uint32_t min_svn;
	uint8_t root_pk[FORTRESS_PK_LEN];
	uint8_t image_pk[FORTRESS_PK_LEN];
	uint8_t root_hash[FORTRESS_HASH_LEN];
};

static struct trust_disk trust;
static uint8_t chunk_hash[CHUNK_MAX][FORTRESS_HASH_LEN];
static uint8_t payload_hash[FORTRESS_HASH_LEN];
static uint32_t chunk_count;
static uint32_t chunk_size;
static uint32_t payload_size;
static uint32_t next_index;
static uint32_t image_svn;
static int begun;
static int verified;
static TEE_OperationHandle running;

static TEE_Result sha256(const void *data, size_t len, uint8_t out[32])
{
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	size_t sz = 32;
	TEE_Result rc;

	rc = TEE_AllocateOperation(&op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
	if (rc != TEE_SUCCESS)
		return rc;
	rc = TEE_DigestDoFinal(op, data, len, out, &sz);
	TEE_FreeOperation(op);
	return rc;
}

static TEE_Result publish_keys(void)
{
	TEE_Result rc;

	rc = fortress_pkcs11_import_p256(FORTRESS_P11_ROOT, trust.root_pk);
	if (rc != TEE_SUCCESS)
		return rc;
	return fortress_pkcs11_import_p256(FORTRESS_P11_IMAGE, trust.image_pk);
}

static TEE_Result p256_verify(const uint8_t pk[FORTRESS_PK_LEN], const void *msg,
			      size_t msg_len, const uint8_t sig[FORTRESS_SIG_LEN])
{
	const char *label;
	uint8_t dig[32];
	TEE_Result rc;

	if (pk[0] != 0x04)
		return TEE_ERROR_BAD_PARAMETERS;
	if (TEE_MemCompare(pk, trust.root_pk, FORTRESS_PK_LEN) == 0)
		label = FORTRESS_P11_ROOT;
	else if (TEE_MemCompare(pk, trust.image_pk, FORTRESS_PK_LEN) == 0)
		label = FORTRESS_P11_IMAGE;
	else
		return TEE_ERROR_BAD_PARAMETERS;
	rc = sha256(msg, msg_len, dig);
	if (rc != TEE_SUCCESS)
		return rc;
	return fortress_pkcs11_verify_p256(label, dig, sig);
}

static TEE_Result save_trust(void)
{
	TEE_ObjectHandle obj = TEE_HANDLE_NULL;
	TEE_Result rc;

	rc = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE, STATE_ID, sizeof(STATE_ID),
					TEE_DATA_FLAG_ACCESS_WRITE | TEE_DATA_FLAG_OVERWRITE,
					TEE_HANDLE_NULL, &trust, sizeof(trust), &obj);
	if (rc != TEE_SUCCESS)
		return rc;
	TEE_CloseObject(obj);
	return TEE_SUCCESS;
}

static TEE_Result load_trust(void)
{
	TEE_ObjectHandle obj = TEE_HANDLE_NULL;
	size_t count = 0;
	TEE_Result rc;
	uint8_t hash[32];

	rc = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE, STATE_ID, sizeof(STATE_ID),
				      TEE_DATA_FLAG_ACCESS_READ, &obj);
	if (rc == TEE_SUCCESS) {
		rc = TEE_ReadObjectData(obj, &trust, sizeof(trust), &count);
		TEE_CloseObject(obj);
		if (rc != TEE_SUCCESS || count != sizeof(trust))
			return TEE_ERROR_BAD_STATE;
		rc = publish_keys();
		if (rc == TEE_SUCCESS)
			fortress_log_gap();
		return rc;
	}
	TEE_MemFill(&trust, 0, sizeof(trust));
	trust.kvn = TRUST_INIT_KVN;
	trust.min_svn = TRUST_INIT_SVN;
	TEE_MemMove(trust.root_pk, trust_init_root_pk, FORTRESS_PK_LEN);
	TEE_MemMove(trust.image_pk, trust_init_image_pk, FORTRESS_PK_LEN);
	TEE_MemMove(trust.root_hash, trust_init_root_hash, FORTRESS_HASH_LEN);
	rc = sha256(trust.root_pk, FORTRESS_PK_LEN, hash);
	if (rc != TEE_SUCCESS)
		return rc;
	if (TEE_MemCompare(hash, trust.root_hash, FORTRESS_HASH_LEN) != 0)
		return TEE_ERROR_SECURITY;
	rc = save_trust();
	if (rc != TEE_SUCCESS)
		return rc;
	rc = publish_keys();
	if (rc != TEE_SUCCESS)
		return rc;
	IMSG("factory state created, KVN %u min SVN %u", trust.kvn, trust.min_svn);
	fortress_log_gap();
	return TEE_SUCCESS;
}

static void clear_download(void)
{
	if (running != TEE_HANDLE_NULL) {
		TEE_FreeOperation(running);
		running = TEE_HANDLE_NULL;
	}
	begun = 0;
	verified = 0;
	next_index = 0;
	chunk_count = 0;
}

static uint32_t chunk_len_at(uint32_t index)
{
	uint32_t off;

	if (index + 1u < chunk_count)
		return chunk_size;
	off = (chunk_count - 1u) * chunk_size;
	return payload_size - off;
}

static int only_ota(void)
{
	return fortress_client_is_gid(FORTRESS_GID_OTA);
}

static TEE_Result cmd_status(uint32_t pt, TEE_Param params[4])
{
	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_VALUE_OUTPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	params[0].value.a = trust.kvn;
	params[0].value.b = trust.min_svn;
	return TEE_SUCCESS;
}

static TEE_Result cmd_manifest(uint32_t pt, TEE_Param params[4])
{
	const uint8_t *m;
	uint8_t hash[32];
	uint32_t kvn;
	TEE_Result rc;

	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[0].memref.size != FORTRESS_MANIFEST_LEN)
		return TEE_ERROR_BAD_PARAMETERS;
	m = params[0].memref.buffer;
	if ((m[0] | ((uint32_t)m[1] << 8) | ((uint32_t)m[2] << 16) | ((uint32_t)m[3] << 24)) !=
	    FORTRESS_MAGIC_FKM1)
		return TEE_ERROR_BAD_PARAMETERS;
	if (((uint16_t)m[6] | ((uint16_t)m[7] << 8)) != FORTRESS_ECU_ID)
		return TEE_ERROR_BAD_PARAMETERS;
	rc = sha256(trust.root_pk, FORTRESS_PK_LEN, hash);
	if (rc != TEE_SUCCESS)
		return rc;
	if (TEE_MemCompare(hash, trust.root_hash, FORTRESS_HASH_LEN) != 0)
		return TEE_ERROR_SECURITY;
	rc = p256_verify(trust.root_pk, m, FORTRESS_MANIFEST_BODY, m + FORTRESS_MANIFEST_BODY);
	if (rc != TEE_SUCCESS) {
		IMSG("manifest signature rejected");
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	kvn = (uint32_t)m[8] | ((uint32_t)m[9] << 8) | ((uint32_t)m[10] << 16) |
	      ((uint32_t)m[11] << 24);
	if (kvn <= trust.kvn) {
		IMSG("manifest KVN %u is not greater than %u", kvn, trust.kvn);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	rc = fortress_pkcs11_import_p256(FORTRESS_P11_IMAGE, m + 20);
	if (rc != TEE_SUCCESS)
		return rc;
	TEE_MemMove(trust.image_pk, m + 20, FORTRESS_PK_LEN);
	trust.kvn = kvn;
	rc = save_trust();
	if (rc != TEE_SUCCESS)
		return rc;
	clear_download();
	IMSG("image public key updated, KVN %u", trust.kvn);
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_begin(uint32_t pt, TEE_Param params[4])
{
	const uint8_t *header;
	uint32_t count;
	uint32_t svn;
	uint32_t kvn;
	size_t body;
	TEE_Result rc;

	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	header = params[0].memref.buffer;
	if (params[0].memref.size < FORTRESS_IMAGE_FIXED + FORTRESS_SIG_LEN)
		return TEE_ERROR_BAD_PARAMETERS;
	if (((uint32_t)header[0] | ((uint32_t)header[1] << 8) | ((uint32_t)header[2] << 16) |
	     ((uint32_t)header[3] << 24)) != FORTRESS_MAGIC_FIMG)
		return TEE_ERROR_BAD_PARAMETERS;
	if (((uint16_t)header[6] | ((uint16_t)header[7] << 8)) != FORTRESS_ECU_ID)
		return TEE_ERROR_BAD_PARAMETERS;
	count = (uint32_t)header[24] | ((uint32_t)header[25] << 8) |
		((uint32_t)header[26] << 16) | ((uint32_t)header[27] << 24);
	chunk_size = (uint32_t)header[20] | ((uint32_t)header[21] << 8) |
		     ((uint32_t)header[22] << 16) | ((uint32_t)header[23] << 24);
	payload_size = (uint32_t)header[16] | ((uint32_t)header[17] << 8) |
		       ((uint32_t)header[18] << 16) | ((uint32_t)header[19] << 24);
	svn = (uint32_t)header[8] | ((uint32_t)header[9] << 8) | ((uint32_t)header[10] << 16) |
	      ((uint32_t)header[11] << 24);
	kvn = (uint32_t)header[12] | ((uint32_t)header[13] << 8) |
	      ((uint32_t)header[14] << 16) | ((uint32_t)header[15] << 24);
	if (!count || count > CHUNK_MAX || !chunk_size || !payload_size)
		return TEE_ERROR_BAD_PARAMETERS;
	body = FORTRESS_IMAGE_FIXED + (size_t)count * FORTRESS_HASH_LEN;
	if (params[0].memref.size != body + FORTRESS_SIG_LEN)
		return TEE_ERROR_BAD_PARAMETERS;
	if (kvn != trust.kvn) {
		IMSG("package KVN %u, current KVN %u", kvn, trust.kvn);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	if (svn < trust.min_svn) {
		IMSG("package SVN %u is below min SVN %u", svn, trust.min_svn);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	rc = p256_verify(trust.image_pk, header, body, header + body);
	if (rc != TEE_SUCCESS) {
		IMSG("package signature rejected");
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	clear_download();
	rc = TEE_AllocateOperation(&running, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
	if (rc != TEE_SUCCESS)
		return rc;
	TEE_MemMove(payload_hash, header + 28, FORTRESS_HASH_LEN);
	TEE_MemMove(chunk_hash, header + FORTRESS_IMAGE_FIXED, count * FORTRESS_HASH_LEN);
	chunk_count = count;
	image_svn = svn;
	next_index = 0;
	begun = 1;
	verified = 0;
	IMSG("package accepted SVN %u KVN %u chunks %u", svn, kvn, count);
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_chunk(uint32_t pt, TEE_Param params[4])
{
	uint32_t index;
	uint32_t expect;
	uint8_t got[32];
	int retransmit;
	TEE_Result rc;

	if (!begun || verified)
		return TEE_ERROR_BAD_STATE;
	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_VALUE_INPUT ||
	    TEE_PARAM_TYPE_GET(pt, 1) != TEE_PARAM_TYPE_MEMREF_INPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	index = params[0].value.a;
	if (index == next_index)
		retransmit = 0;
	else if (next_index > 0 && index + 1u == next_index)
		retransmit = 1;
	else {
		IMSG("chunk %u rejected, next is %u", index, next_index);
		fortress_log_gap();
		return TEE_ERROR_BAD_PARAMETERS;
	}
	if (index >= chunk_count)
		return TEE_ERROR_BAD_PARAMETERS;
	expect = chunk_len_at(index);
	if (params[1].memref.size != expect)
		return TEE_ERROR_BAD_PARAMETERS;
	rc = sha256(params[1].memref.buffer, params[1].memref.size, got);
	if (rc != TEE_SUCCESS)
		return rc;
	if (TEE_MemCompare(got, chunk_hash[index], FORTRESS_HASH_LEN) != 0) {
		IMSG("chunk %u hash mismatch", index);
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	if (retransmit) {
		IMSG("chunk %u retransmit", index);
		fortress_log_gap();
		return TEE_SUCCESS;
	}
	TEE_DigestUpdate(running, params[1].memref.buffer, params[1].memref.size);
	next_index++;
	IMSG("chunk %u ok", index);
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_verify(void)
{
	uint8_t got[32];
	size_t sz = 32;
	TEE_Result rc;

	if (!begun || next_index != chunk_count)
		return TEE_ERROR_BAD_STATE;
	rc = TEE_DigestDoFinal(running, NULL, 0, got, &sz);
	TEE_FreeOperation(running);
	running = TEE_HANDLE_NULL;
	if (rc != TEE_SUCCESS)
		return rc;
	if (TEE_MemCompare(got, payload_hash, FORTRESS_HASH_LEN) != 0) {
		IMSG("payload hash mismatch");
		fortress_log_gap();
		return TEE_ERROR_SECURITY;
	}
	verified = 1;
	IMSG("package verified");
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_activate(void)
{
	TEE_Result rc;

	if (!verified)
		return TEE_ERROR_BAD_STATE;
	if (image_svn < trust.min_svn)
		return TEE_ERROR_SECURITY;
	if (image_svn > trust.min_svn)
		trust.min_svn = image_svn;
	rc = save_trust();
	if (rc != TEE_SUCCESS)
		return rc;
	IMSG("activated, min SVN %u", trust.min_svn);
	fortress_log_gap();
	return TEE_SUCCESS;
}

TEE_Result TA_CreateEntryPoint(void)
{
	running = TEE_HANDLE_NULL;
	return load_trust();
}

void TA_DestroyEntryPoint(void)
{
	clear_download();
}

static TEE_Result cmd_seed(struct ota_sess *s, uint32_t pt, TEE_Param params[4])
{
	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[0].memref.size < SEED_LEN)
		return TEE_ERROR_SHORT_BUFFER;
	TEE_GenerateRandom(s->seed, SEED_LEN);
	TEE_MemMove(params[0].memref.buffer, s->seed, SEED_LEN);
	params[0].memref.size = SEED_LEN;
	s->have_seed = 1;
	s->unlocked = 0;
	IMSG("seed issued");
	fortress_log_gap();
	return TEE_SUCCESS;
}

static TEE_Result cmd_unlock(struct ota_sess *s, uint32_t pt, TEE_Param params[4])
{
	TEE_Result rc;

	if (!s->have_seed)
		return TEE_ERROR_BAD_STATE;
	if (TEE_PARAM_TYPE_GET(pt, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[0].memref.size != FORTRESS_SIG_LEN)
		return TEE_ERROR_BAD_PARAMETERS;
	rc = p256_verify(trust.image_pk, s->seed, SEED_LEN, params[0].memref.buffer);
	if (rc != TEE_SUCCESS) {
		IMSG("security access rejected");
		fortress_log_gap();
		return rc;
	}
	s->unlocked = 1;
	IMSG("security access unlocked");
	fortress_log_gap();
	return TEE_SUCCESS;
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t pt, TEE_Param params[4], void **sess)
{
	struct ota_sess *s;

	(void)pt;
	(void)params;
	if (!only_ota())
		return TEE_ERROR_ACCESS_DENIED;
	s = TEE_Malloc(sizeof(*s), TEE_MALLOC_FILL_ZERO);
	if (!s)
		return TEE_ERROR_OUT_OF_MEMORY;
	*sess = s;
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess)
{
	TEE_Free(sess);
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess, uint32_t cmd, uint32_t pt,
				      TEE_Param params[4])
{
	struct ota_sess *s = sess;

	if (!only_ota())
		return TEE_ERROR_ACCESS_DENIED;
	switch (cmd) {
	case CMD_GET_STATUS:
		return cmd_status(pt, params);
	case CMD_GET_SEED:
		return cmd_seed(s, pt, params);
	case CMD_UNLOCK:
		return cmd_unlock(s, pt, params);
	case CMD_UPDATE_KEY_MANIFEST:
	case CMD_IMAGE_BEGIN:
	case CMD_IMAGE_UPDATE:
	case CMD_IMAGE_VERIFY:
	case CMD_IMAGE_ACTIVATE:
		if (!s->unlocked)
			return TEE_ERROR_ACCESS_DENIED;
		break;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
	switch (cmd) {
	case CMD_UPDATE_KEY_MANIFEST:
		return cmd_manifest(pt, params);
	case CMD_IMAGE_BEGIN:
		return cmd_begin(pt, params);
	case CMD_IMAGE_UPDATE:
		return cmd_chunk(pt, params);
	case CMD_IMAGE_VERIFY:
		return cmd_verify();
	case CMD_IMAGE_ACTIVATE:
		return cmd_activate();
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
