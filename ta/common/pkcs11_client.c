#include "pkcs11_client.h"

#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

/*
 * Numbers match dep-src/optee_os/ta/pkcs11/include/pkcs11_ta.h (TA version 1).
 * libckteec's copy of that header is a different version. Do not use it here.
 * This file speaks the TA command ABI. It does not link libckteec.
 */

#define PKCS11_CMD_TOKEN_INFO 3
#define PKCS11_CMD_OPEN_SESSION 6
#define PKCS11_CMD_INIT_TOKEN 10
#define PKCS11_CMD_INIT_PIN 11
#define PKCS11_CMD_LOGIN 13
#define PKCS11_CMD_LOGOUT 14
#define PKCS11_CMD_CREATE_OBJECT 15
#define PKCS11_CMD_DESTROY_OBJECT 16
#define PKCS11_CMD_ENCRYPT_INIT 17
#define PKCS11_CMD_DECRYPT_INIT 18
#define PKCS11_CMD_ENCRYPT_ONESHOT 23
#define PKCS11_CMD_DECRYPT_ONESHOT 24
#define PKCS11_CMD_VERIFY_INIT 26
#define PKCS11_CMD_VERIFY_ONESHOT 32
#define PKCS11_CMD_FIND_OBJECTS_INIT 34
#define PKCS11_CMD_FIND_OBJECTS 35
#define PKCS11_CMD_FIND_OBJECTS_FINAL 36

#define PKCS11_CKR_OK 0
#define PKCS11_CKFSS_RW_SESSION (1U << 1)
#define PKCS11_CKFSS_SERIAL_SESSION (1U << 2)
#define PKCS11_CKFT_USER_PIN_INITIALIZED (1U << 3)
#define PKCS11_CKFT_TOKEN_INITIALIZED (1U << 10)
#define PKCS11_CKU_SO 0
#define PKCS11_CKU_USER 1
#define PKCS11_CKO_PUBLIC_KEY 0x0002
#define PKCS11_CKO_SECRET_KEY 0x0004
#define PKCS11_CKK_EC 0x0003
#define PKCS11_CKK_AES 0x001f
#define PKCS11_CKA_CLASS 0x0000
#define PKCS11_CKA_TOKEN 0x0001
#define PKCS11_CKA_PRIVATE 0x0002
#define PKCS11_CKA_LABEL 0x0003
#define PKCS11_CKA_VALUE 0x0011
#define PKCS11_CKA_KEY_TYPE 0x0100
#define PKCS11_CKA_SENSITIVE 0x0103
#define PKCS11_CKA_ENCRYPT 0x0104
#define PKCS11_CKA_DECRYPT 0x0105
#define PKCS11_CKA_VERIFY 0x010a
#define PKCS11_CKA_EC_PARAMS 0x0180
#define PKCS11_CKA_EC_POINT 0x0181
#define PKCS11_CKM_ECDSA 0x01041
#define PKCS11_CKM_AES_GCM 0x01087
#define PKCS11_TOKEN_LABEL_SIZE 32

#define SLOT_ID 0
#define SO_PIN "so-fortress"
#define USER_PIN "fortress"
#define TOKEN_LABEL "fortress"

static const TEE_UUID pkcs11_uuid = {
	0xfd02c9da, 0x306c, 0x48c7,
	{ 0xa4, 0x9c, 0xbb, 0xd8, 0x27, 0xae, 0x86, 0xee }
};

/* Same layout as struct pkcs11_token_info. flags sits at byte 96. */
struct token_info {
	uint8_t label[32];
	uint8_t manufacturer_id[32];
	uint8_t model[16];
	uint8_t serial_number[16];
	uint32_t flags;
	uint32_t max_session_count;
	uint32_t session_count;
	uint32_t max_rw_session_count;
	uint32_t rw_session_count;
	uint32_t max_pin_len;
	uint32_t min_pin_len;
	uint32_t total_public_memory;
	uint32_t free_public_memory;
	uint32_t total_private_memory;
	uint32_t free_private_memory;
	uint8_t hardware_version[2];
	uint8_t firmware_version[2];
	uint8_t utc_time[16];
};

static const uint8_t prime256v1[] = {
	0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07
};

struct bytebuf {
	uint8_t *p;
	uint32_t n;
	uint32_t cap;
};

static TEE_TASessionHandle ta_sess;
static uint32_t ck_sess;
static int ready;

static int put_bytes(struct bytebuf *b, const void *data, uint32_t len)
{
	if (b->n + len > b->cap)
		return -1;
	if (len)
		TEE_MemMove(b->p + b->n, data, len);
	b->n += len;
	return 0;
}

static int put_u32(struct bytebuf *b, uint32_t v)
{
	return put_bytes(b, &v, sizeof(v));
}

static int put_attr(struct bytebuf *b, uint32_t id, const void *data, uint32_t len)
{
	if (put_u32(b, id) || put_u32(b, len))
		return -1;
	return put_bytes(b, data, len);
}

static TEE_Result map_rc(const char *what, uint32_t rc)
{
	if (rc == PKCS11_CKR_OK)
		return TEE_SUCCESS;
	IMSG("pkcs11 %s rc 0x%x", what, rc);
	return TEE_ERROR_SECURITY;
}

static TEE_Result invoke(uint32_t cmd, const char *what, uint32_t types, TEE_Param params[4])
{
	TEE_Result res;
	uint32_t rc = 0;

	res = TEE_InvokeTACommand(ta_sess, TEE_TIMEOUT_INFINITE, cmd, types, params, NULL);
	if (res != TEE_SUCCESS && res != TEE_ERROR_SHORT_BUFFER) {
		IMSG("pkcs11 %s invoke 0x%x", what, res);
		return res;
	}
	if (params[0].memref.size < sizeof(rc))
		return TEE_ERROR_BAD_FORMAT;
	TEE_MemMove(&rc, params[0].memref.buffer, sizeof(rc));
	if (res == TEE_ERROR_SHORT_BUFFER)
		return map_rc(what, rc ? rc : 0x150);
	return map_rc(what, rc);
}

static TEE_Result cmd_ctrl(uint32_t cmd, const char *what, struct bytebuf *ctrl)
{
	TEE_Param params[4];
	uint32_t types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT, TEE_PARAM_TYPE_NONE,
					 TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

	TEE_MemFill(params, 0, sizeof(params));
	params[0].memref.buffer = ctrl->p;
	params[0].memref.size = ctrl->n;
	return invoke(cmd, what, types, params);
}

static TEE_Result cmd_out(uint32_t cmd, const char *what, struct bytebuf *ctrl,
			  void *out, uint32_t out_len, uint32_t *out_got)
{
	TEE_Param params[4];
	uint32_t types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT, TEE_PARAM_TYPE_NONE,
					 TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE);
	TEE_Result res;

	TEE_MemFill(params, 0, sizeof(params));
	params[0].memref.buffer = ctrl->p;
	params[0].memref.size = ctrl->n;
	params[2].memref.buffer = out;
	params[2].memref.size = out_len;
	res = invoke(cmd, what, types, params);
	if (out_got)
		*out_got = params[2].memref.size;
	return res;
}

static TEE_Result token_flags(uint32_t *flags)
{
	struct token_info info;
	struct bytebuf ctrl;
	uint8_t raw[8];
	uint32_t slot = SLOT_ID;
	TEE_Result res;

	TEE_MemFill(&info, 0, sizeof(info));
	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, slot))
		return TEE_ERROR_BAD_STATE;
	res = cmd_out(PKCS11_CMD_TOKEN_INFO, "token info", &ctrl, &info, sizeof(info), NULL);
	if (res != TEE_SUCCESS)
		return res;
	*flags = info.flags;
	return TEE_SUCCESS;
}

static TEE_Result init_token(void)
{
	struct bytebuf ctrl;
	uint8_t raw[80];
	uint8_t label[PKCS11_TOKEN_LABEL_SIZE];
	uint32_t pin_len = sizeof(SO_PIN) - 1;

	TEE_MemFill(label, ' ', sizeof(label));
	TEE_MemMove(label, TOKEN_LABEL, sizeof(TOKEN_LABEL) - 1);
	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, SLOT_ID) || put_u32(&ctrl, pin_len) ||
	    put_bytes(&ctrl, label, sizeof(label)) ||
	    put_bytes(&ctrl, SO_PIN, pin_len))
		return TEE_ERROR_BAD_STATE;
	return cmd_ctrl(PKCS11_CMD_INIT_TOKEN, "init token", &ctrl);
}

static TEE_Result open_ck_session(void)
{
	struct bytebuf ctrl;
	uint8_t raw[16];
	uint32_t handle = 0;
	uint32_t flags = PKCS11_CKFSS_RW_SESSION | PKCS11_CKFSS_SERIAL_SESSION;
	TEE_Result res;

	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, SLOT_ID) || put_u32(&ctrl, flags))
		return TEE_ERROR_BAD_STATE;
	res = cmd_out(PKCS11_CMD_OPEN_SESSION, "open session", &ctrl, &handle, sizeof(handle),
		      NULL);
	if (res != TEE_SUCCESS)
		return res;
	ck_sess = handle;
	return TEE_SUCCESS;
}

static TEE_Result login(uint32_t user, const char *pin)
{
	struct bytebuf ctrl;
	uint8_t raw[64];
	uint32_t pin_len = strlen(pin);

	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess) || put_u32(&ctrl, user) || put_u32(&ctrl, pin_len) ||
	    put_bytes(&ctrl, pin, pin_len))
		return TEE_ERROR_BAD_STATE;
	return cmd_ctrl(PKCS11_CMD_LOGIN, "login", &ctrl);
}

static TEE_Result logout(void)
{
	struct bytebuf ctrl;
	uint8_t raw[8];

	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess))
		return TEE_ERROR_BAD_STATE;
	return cmd_ctrl(PKCS11_CMD_LOGOUT, "logout", &ctrl);
}

static TEE_Result init_user_pin(void)
{
	struct bytebuf ctrl;
	uint8_t raw[32];
	uint32_t pin_len = sizeof(USER_PIN) - 1;

	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess) || put_u32(&ctrl, pin_len) ||
	    put_bytes(&ctrl, USER_PIN, pin_len))
		return TEE_ERROR_BAD_STATE;
	return cmd_ctrl(PKCS11_CMD_INIT_PIN, "init pin", &ctrl);
}

static TEE_Result ensure(void)
{
	TEE_Result res;
	uint32_t types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
					 TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);
	uint32_t flags = 0;

	if (ready)
		return TEE_SUCCESS;
	res = TEE_OpenTASession(&pkcs11_uuid, TEE_TIMEOUT_INFINITE, types, NULL, &ta_sess, NULL);
	if (res != TEE_SUCCESS) {
		IMSG("pkcs11 open session 0x%x", res);
		return res;
	}
	res = token_flags(&flags);
	if (res != TEE_SUCCESS)
		goto fail;
	if (!(flags & PKCS11_CKFT_TOKEN_INITIALIZED)) {
		res = init_token();
		if (res != TEE_SUCCESS)
			goto fail;
		res = token_flags(&flags);
		if (res != TEE_SUCCESS)
			goto fail;
	}
	res = open_ck_session();
	if (res != TEE_SUCCESS)
		goto fail;
	if (!(flags & PKCS11_CKFT_USER_PIN_INITIALIZED)) {
		res = login(PKCS11_CKU_SO, SO_PIN);
		if (res != TEE_SUCCESS)
			goto fail;
		res = init_user_pin();
		if (res != TEE_SUCCESS)
			goto fail;
		res = logout();
		if (res != TEE_SUCCESS)
			goto fail;
	}
	res = login(PKCS11_CKU_USER, USER_PIN);
	if (res != TEE_SUCCESS)
		goto fail;
	ready = 1;
	return TEE_SUCCESS;
fail:
	TEE_CloseTASession(ta_sess);
	ta_sess = TEE_HANDLE_NULL;
	ready = 0;
	return res;
}

static int put_head(struct bytebuf *b, uint32_t count, uint32_t *size_at)
{
	uint32_t zero = 0;

	*size_at = b->n;
	if (put_u32(b, zero) || put_u32(b, count))
		return -1;
	return 0;
}

static void finish_head(struct bytebuf *b, uint32_t size_at, uint32_t attrs_at)
{
	uint32_t attrs_size = b->n - attrs_at;

	TEE_MemMove(b->p + size_at, &attrs_size, sizeof(attrs_size));
}

static TEE_Result find_label(uint32_t class_id, const char *label, uint32_t *handles,
			     uint32_t *count)
{
	struct bytebuf ctrl;
	uint8_t raw[256];
	uint32_t size_at = 0;
	uint32_t attrs_at;
	uint32_t got = 0;
	uint8_t out[16];
	TEE_Result res;

	*count = 0;
	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess) || put_head(&ctrl, 2, &size_at))
		return TEE_ERROR_BAD_STATE;
	attrs_at = ctrl.n;
	if (put_attr(&ctrl, PKCS11_CKA_CLASS, &class_id, sizeof(class_id)) ||
	    put_attr(&ctrl, PKCS11_CKA_LABEL, label, strlen(label)))
		return TEE_ERROR_BAD_STATE;
	finish_head(&ctrl, size_at, attrs_at);
	res = cmd_ctrl(PKCS11_CMD_FIND_OBJECTS_INIT, "find init", &ctrl);
	if (res != TEE_SUCCESS)
		return res;
	ctrl.n = 0;
	if (put_u32(&ctrl, ck_sess))
		return TEE_ERROR_BAD_STATE;
	res = cmd_out(PKCS11_CMD_FIND_OBJECTS, "find", &ctrl, out, sizeof(out), &got);
	/* The command overwrites the control buffer with the return code. */
	ctrl.n = 0;
	if (put_u32(&ctrl, ck_sess))
		return TEE_ERROR_BAD_STATE;
	if (cmd_ctrl(PKCS11_CMD_FIND_OBJECTS_FINAL, "find final", &ctrl) != TEE_SUCCESS &&
	    res == TEE_SUCCESS)
		return TEE_ERROR_GENERIC;
	if (res != TEE_SUCCESS)
		return res;
	if (got > sizeof(out) || (got % sizeof(uint32_t)) != 0)
		return TEE_ERROR_BAD_FORMAT;
	*count = got / sizeof(uint32_t);
	if (*count)
		TEE_MemMove(handles, out, got);
	return TEE_SUCCESS;
}

static TEE_Result destroy_handle(uint32_t handle)
{
	struct bytebuf ctrl;
	uint8_t raw[16];

	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess) || put_u32(&ctrl, handle))
		return TEE_ERROR_BAD_STATE;
	return cmd_ctrl(PKCS11_CMD_DESTROY_OBJECT, "destroy", &ctrl);
}

static TEE_Result replace_object(struct bytebuf *body)
{
	uint32_t handles[4];
	uint32_t count = 0;
	uint32_t i;
	uint32_t new_handle = 0;
	TEE_Result res;
	struct bytebuf ctrl;
	uint8_t raw[512];
	uint32_t class_id = 0;
	uint32_t label_len = 0;
	const uint8_t *label = NULL;
	const uint8_t *p;
	uint32_t left;

	/* The body starts with the object head. Walk it for class and label. */
	p = body->p + 8;
	left = body->n - 8;
	while (left >= 8) {
		uint32_t id;
		uint32_t sz;

		TEE_MemMove(&id, p, 4);
		TEE_MemMove(&sz, p + 4, 4);
		if (8 + sz > left)
			return TEE_ERROR_BAD_STATE;
		if (id == PKCS11_CKA_CLASS && sz == sizeof(class_id))
			TEE_MemMove(&class_id, p + 8, sizeof(class_id));
		if (id == PKCS11_CKA_LABEL) {
			label = p + 8;
			label_len = sz;
		}
		p += 8 + sz;
		left -= 8 + sz;
	}
	if (!label)
		return TEE_ERROR_BAD_STATE;
	{
		char name[32];

		if (label_len >= sizeof(name))
			return TEE_ERROR_BAD_STATE;
		TEE_MemMove(name, label, label_len);
		name[label_len] = 0;
		res = find_label(class_id, name, handles, &count);
		if (res != TEE_SUCCESS)
			return res;
	}
	for (i = 0; i < count; i++) {
		res = destroy_handle(handles[i]);
		if (res != TEE_SUCCESS)
			return res;
	}
	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess) || put_bytes(&ctrl, body->p, body->n))
		return TEE_ERROR_BAD_STATE;
	res = cmd_out(PKCS11_CMD_CREATE_OBJECT, "create", &ctrl, &new_handle, sizeof(new_handle),
		      NULL);
	if (res == TEE_SUCCESS)
		IMSG("pkcs11 object stored");
	return res;
}

static int put_bool(struct bytebuf *b, uint32_t id, uint8_t v)
{
	return put_attr(b, id, &v, 1);
}

TEE_Result fortress_pkcs11_import_aes(const uint8_t key[16])
{
	struct bytebuf body;
	uint8_t raw[256];
	uint32_t size_at = 0;
	uint32_t attrs_at;
	uint32_t class_id = PKCS11_CKO_SECRET_KEY;
	uint32_t key_type = PKCS11_CKK_AES;
	TEE_Result res;

	res = ensure();
	if (res != TEE_SUCCESS)
		return res;
	body.p = raw;
	body.n = 0;
	body.cap = sizeof(raw);
	if (put_head(&body, 9, &size_at))
		return TEE_ERROR_BAD_STATE;
	attrs_at = body.n;
	if (put_attr(&body, PKCS11_CKA_CLASS, &class_id, sizeof(class_id)) ||
	    put_attr(&body, PKCS11_CKA_KEY_TYPE, &key_type, sizeof(key_type)) ||
	    put_bool(&body, PKCS11_CKA_TOKEN, 1) || put_bool(&body, PKCS11_CKA_PRIVATE, 1) ||
	    put_bool(&body, PKCS11_CKA_SENSITIVE, 1) || put_bool(&body, PKCS11_CKA_ENCRYPT, 1) ||
	    put_bool(&body, PKCS11_CKA_DECRYPT, 1) ||
	    put_attr(&body, PKCS11_CKA_LABEL, FORTRESS_P11_SOA, sizeof(FORTRESS_P11_SOA) - 1) ||
	    put_attr(&body, PKCS11_CKA_VALUE, key, 16))
		return TEE_ERROR_BAD_STATE;
	finish_head(&body, size_at, attrs_at);
	return replace_object(&body);
}

TEE_Result fortress_pkcs11_import_p256(const char *label, const uint8_t point[65])
{
	struct bytebuf body;
	uint8_t raw[256];
	uint8_t der[67];
	uint32_t size_at = 0;
	uint32_t attrs_at;
	uint32_t class_id = PKCS11_CKO_PUBLIC_KEY;
	uint32_t key_type = PKCS11_CKK_EC;
	TEE_Result res;

	if (point[0] != 0x04)
		return TEE_ERROR_BAD_PARAMETERS;
	res = ensure();
	if (res != TEE_SUCCESS)
		return res;
	der[0] = 0x04;
	der[1] = 65;
	TEE_MemMove(der + 2, point, 65);
	body.p = raw;
	body.n = 0;
	body.cap = sizeof(raw);
	if (put_head(&body, 7, &size_at))
		return TEE_ERROR_BAD_STATE;
	attrs_at = body.n;
	if (put_attr(&body, PKCS11_CKA_CLASS, &class_id, sizeof(class_id)) ||
	    put_attr(&body, PKCS11_CKA_KEY_TYPE, &key_type, sizeof(key_type)) ||
	    put_bool(&body, PKCS11_CKA_TOKEN, 1) || put_bool(&body, PKCS11_CKA_VERIFY, 1) ||
	    put_attr(&body, PKCS11_CKA_LABEL, label, strlen(label)) ||
	    put_attr(&body, PKCS11_CKA_EC_PARAMS, prime256v1, sizeof(prime256v1)) ||
	    put_attr(&body, PKCS11_CKA_EC_POINT, der, sizeof(der)))
		return TEE_ERROR_BAD_STATE;
	finish_head(&body, size_at, attrs_at);
	return replace_object(&body);
}

int fortress_pkcs11_has_aes(void)
{
	uint32_t handles[4];
	uint32_t count = 0;

	if (ensure() != TEE_SUCCESS)
		return 0;
	if (find_label(PKCS11_CKO_SECRET_KEY, FORTRESS_P11_SOA, handles, &count) != TEE_SUCCESS)
		return 0;
	return count > 0;
}

static TEE_Result object_handle(uint32_t class_id, const char *label, uint32_t *handle)
{
	uint32_t handles[4];
	uint32_t count = 0;
	TEE_Result res;

	res = find_label(class_id, label, handles, &count);
	if (res != TEE_SUCCESS)
		return res;
	if (count != 1)
		return TEE_ERROR_ITEM_NOT_FOUND;
	*handle = handles[0];
	return TEE_SUCCESS;
}

static TEE_Result processing_init(uint32_t cmd, const char *what, uint32_t handle,
				  uint32_t mechanism, const void *mech, uint32_t mech_len)
{
	struct bytebuf ctrl;
	uint8_t raw[128];

	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess) || put_u32(&ctrl, handle) ||
	    put_attr(&ctrl, mechanism, mech, mech_len))
		return TEE_ERROR_BAD_STATE;
	return cmd_ctrl(cmd, what, &ctrl);
}

TEE_Result fortress_pkcs11_aes_gcm(int encrypt, const uint8_t iv[12], const uint8_t aad[10],
				   const uint8_t *in, uint32_t in_len, uint8_t *out,
				   uint8_t tag[16])
{
	uint8_t mech[4 + 12 + 4 + 10 + 4];
	uint32_t iv_len = 12;
	uint32_t aad_len = 10;
	uint32_t tag_bits = 128;
	uint32_t handle = 0;
	uint8_t *ct = NULL;
	uint8_t *produced = NULL;
	uint32_t produced_len;
	struct bytebuf ctrl;
	uint8_t raw[8];
	TEE_Param params[4];
	uint32_t types;
	uint32_t got = 0;
	TEE_Result res;

	res = ensure();
	if (res != TEE_SUCCESS)
		return res;
	res = object_handle(PKCS11_CKO_SECRET_KEY, FORTRESS_P11_SOA, &handle);
	if (res != TEE_SUCCESS)
		return res;
	TEE_MemMove(mech, &iv_len, 4);
	TEE_MemMove(mech + 4, iv, 12);
	TEE_MemMove(mech + 16, &aad_len, 4);
	TEE_MemMove(mech + 20, aad, 10);
	TEE_MemMove(mech + 30, &tag_bits, 4);
	produced_len = encrypt ? in_len + 16 : in_len;
	produced = TEE_Malloc(produced_len ? produced_len : 1, TEE_MALLOC_FILL_ZERO);
	if (!produced)
		return TEE_ERROR_OUT_OF_MEMORY;
	if (!encrypt) {
		ct = TEE_Malloc(in_len + 16, TEE_MALLOC_FILL_ZERO);
		if (!ct) {
			TEE_Free(produced);
			return TEE_ERROR_OUT_OF_MEMORY;
		}
		TEE_MemMove(ct, in, in_len);
		TEE_MemMove(ct + in_len, tag, 16);
	}
	res = processing_init(encrypt ? PKCS11_CMD_ENCRYPT_INIT : PKCS11_CMD_DECRYPT_INIT,
			      encrypt ? "encrypt init" : "decrypt init", handle,
			      PKCS11_CKM_AES_GCM, mech, sizeof(mech));
	if (res != TEE_SUCCESS)
		goto out;
	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess)) {
		res = TEE_ERROR_BAD_STATE;
		goto out;
	}
	TEE_MemFill(params, 0, sizeof(params));
	params[0].memref.buffer = ctrl.p;
	params[0].memref.size = ctrl.n;
	if (encrypt) {
		params[1].memref.buffer = (void *)in;
		params[1].memref.size = in_len;
		params[2].memref.buffer = produced;
		params[2].memref.size = in_len + 16;
	} else {
		params[1].memref.buffer = ct;
		params[1].memref.size = in_len + 16;
		params[2].memref.buffer = produced;
		params[2].memref.size = in_len;
	}
	types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT, TEE_PARAM_TYPE_MEMREF_INPUT,
				TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE);
	res = invoke(encrypt ? PKCS11_CMD_ENCRYPT_ONESHOT : PKCS11_CMD_DECRYPT_ONESHOT,
		     encrypt ? "encrypt" : "decrypt", types, params);
	got = params[2].memref.size;
	if (res == TEE_SUCCESS && encrypt) {
		if (got != in_len + 16)
			res = TEE_ERROR_BAD_FORMAT;
		else {
			TEE_MemMove(out, produced, in_len);
			TEE_MemMove(tag, produced + in_len, 16);
		}
	} else if (res == TEE_SUCCESS) {
		if (got != in_len)
			res = TEE_ERROR_BAD_FORMAT;
		else if (in_len)
			TEE_MemMove(out, produced, in_len);
	}
out:
	TEE_Free(ct);
	TEE_Free(produced);
	return res;
}

TEE_Result fortress_pkcs11_verify_p256(const char *label, const uint8_t digest[32],
				       const uint8_t sig[64])
{
	uint32_t handle = 0;
	struct bytebuf ctrl;
	uint8_t raw[8];
	TEE_Param params[4];
	uint32_t types;
	TEE_Result res;

	res = ensure();
	if (res != TEE_SUCCESS)
		return res;
	res = object_handle(PKCS11_CKO_PUBLIC_KEY, label, &handle);
	if (res != TEE_SUCCESS)
		return res;
	res = processing_init(PKCS11_CMD_VERIFY_INIT, "verify init", handle, PKCS11_CKM_ECDSA,
			      NULL, 0);
	if (res != TEE_SUCCESS)
		return res;
	ctrl.p = raw;
	ctrl.n = 0;
	ctrl.cap = sizeof(raw);
	if (put_u32(&ctrl, ck_sess))
		return TEE_ERROR_BAD_STATE;
	TEE_MemFill(params, 0, sizeof(params));
	params[0].memref.buffer = ctrl.p;
	params[0].memref.size = ctrl.n;
	params[1].memref.buffer = (void *)digest;
	params[1].memref.size = 32;
	params[2].memref.buffer = (void *)sig;
	params[2].memref.size = 64;
	types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT, TEE_PARAM_TYPE_MEMREF_INPUT,
				TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_NONE);
	return invoke(PKCS11_CMD_VERIFY_ONESHOT, "verify", types, params);
}
