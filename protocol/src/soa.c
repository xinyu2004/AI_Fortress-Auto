#include "fortress/soa.h"

#include "fortress/codec.h"
#include "fortress/status.h"

#include <openssl/evp.h>
#include <string.h>

static int fresh_slot(fortress_fresh_table *fresh, uint16_t service_id, int create)
{
	int i;
	int free_slot = -1;

	for (i = 0; i < FORTRESS_FRESH_SLOTS; i++) {
		if (fresh->slot[i].present && fresh->slot[i].service_id == service_id)
			return i;
		if (!fresh->slot[i].present && free_slot < 0)
			free_slot = i;
	}
	if (!create || free_slot < 0)
		return -1;
	fresh->slot[free_slot].service_id = service_id;
	fresh->slot[free_slot].counter = 0;
	fresh->slot[free_slot].present = 1;
	return free_slot;
}

static void iv_from(uint16_t service_id, uint16_t method_id, uint32_t freshness,
		    uint8_t iv[12])
{
	memset(iv, 0, 12);
	fortress_put_u16(iv, service_id);
	fortress_put_u16(iv + 2, method_id);
	fortress_put_u32(iv + 4, freshness);
}

static int gcm_encrypt(const uint8_t key[FORTRESS_AES_LEN], const uint8_t iv[12],
		       const uint8_t *aad, int aad_len, const uint8_t *plain, int plain_len,
		       uint8_t *ct, uint8_t tag[FORTRESS_GCM_TAG_LEN])
{
	EVP_CIPHER_CTX *ctx;
	int outl = 0;
	int fin = 0;
	int ok;

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return FORTRESS_ERR_CRYPTO;
	ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) == 1 &&
	     EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1 &&
	     EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
	     EVP_EncryptUpdate(ctx, NULL, &outl, aad, aad_len) == 1 &&
	     EVP_EncryptUpdate(ctx, ct, &outl, plain, plain_len) == 1 &&
	     EVP_EncryptFinal_ex(ctx, ct + outl, &fin) == 1 &&
	     EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, FORTRESS_GCM_TAG_LEN, tag) == 1;
	EVP_CIPHER_CTX_free(ctx);
	return ok ? FORTRESS_OK : FORTRESS_ERR_CRYPTO;
}

static int gcm_decrypt(const uint8_t key[FORTRESS_AES_LEN], const uint8_t iv[12],
		       const uint8_t *aad, int aad_len, const uint8_t *ct, int ct_len,
		       const uint8_t tag[FORTRESS_GCM_TAG_LEN], uint8_t *plain)
{
	EVP_CIPHER_CTX *ctx;
	int outl = 0;
	int fin = 0;
	int ok;

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return FORTRESS_ERR_CRYPTO;
	ok = EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) == 1 &&
	     EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1 &&
	     EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
	     EVP_DecryptUpdate(ctx, NULL, &outl, aad, aad_len) == 1 &&
	     EVP_DecryptUpdate(ctx, plain, &outl, ct, ct_len) == 1 &&
	     EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, FORTRESS_GCM_TAG_LEN,
				 (void *)tag) == 1 &&
	     EVP_DecryptFinal_ex(ctx, plain + outl, &fin) == 1;
	EVP_CIPHER_CTX_free(ctx);
	return ok ? FORTRESS_OK : FORTRESS_ERR_SIGNATURE;
}

int fortress_soa_seal(fortress_fresh_table *fresh, uint16_t service_id,
		      uint16_t method_id, const uint8_t key[FORTRESS_AES_LEN],
		      const uint8_t *plain, uint16_t plain_len,
		      uint8_t *out, size_t out_cap, size_t *out_len)
{
	int slot;
	uint32_t next;
	uint8_t iv[12];
	uint8_t aad[FORTRESS_SOA_HEADER];
	uint8_t tag[FORTRESS_GCM_TAG_LEN];
	size_t need;
	int rc;

	if (!fresh || !key || !plain || !out || !out_len)
		return FORTRESS_ERR_ARG;
	need = FORTRESS_SOA_HEADER + plain_len + FORTRESS_GCM_TAG_LEN;
	if (out_cap < need)
		return FORTRESS_ERR_ARG;
	slot = fresh_slot(fresh, service_id, 1);
	if (slot < 0)
		return FORTRESS_ERR_STATE;
	next = fresh->slot[slot].counter + 1u;
	fortress_put_u16(out, service_id);
	fortress_put_u16(out + 2, method_id);
	fortress_put_u32(out + 4, next);
	fortress_put_u16(out + 8, plain_len);
	memcpy(aad, out, FORTRESS_SOA_HEADER);
	iv_from(service_id, method_id, next, iv);
	rc = gcm_encrypt(key, iv, aad, FORTRESS_SOA_HEADER, plain, plain_len,
			 out + FORTRESS_SOA_HEADER, tag);
	if (rc != FORTRESS_OK)
		return rc;
	memcpy(out + FORTRESS_SOA_HEADER + plain_len, tag, FORTRESS_GCM_TAG_LEN);
	fresh->slot[slot].counter = next;
	*out_len = need;
	return FORTRESS_OK;
}

int fortress_soa_open(fortress_fresh_table *fresh,
		      const uint8_t key[FORTRESS_AES_LEN],
		      const uint8_t *frame, size_t frame_len,
		      uint8_t *plain, size_t plain_cap, size_t *plain_len,
		      uint16_t *service_id, uint16_t *method_id)
{
	uint16_t sid;
	uint16_t mid;
	uint16_t ct_len;
	uint32_t freshness;
	uint8_t iv[12];
	uint8_t aad[FORTRESS_SOA_HEADER];
	int slot;
	int rc;

	if (!fresh || !key || !frame || !plain || !plain_len)
		return FORTRESS_ERR_ARG;
	if (frame_len < FORTRESS_SOA_HEADER + FORTRESS_GCM_TAG_LEN)
		return FORTRESS_ERR_ARG;
	sid = fortress_get_u16(frame);
	mid = fortress_get_u16(frame + 2);
	freshness = fortress_get_u32(frame + 4);
	ct_len = fortress_get_u16(frame + 8);
	if (frame_len != (size_t)FORTRESS_SOA_HEADER + ct_len + FORTRESS_GCM_TAG_LEN)
		return FORTRESS_ERR_ARG;
	if (plain_cap < ct_len)
		return FORTRESS_ERR_ARG;
	slot = fresh_slot(fresh, sid, 1);
	if (slot < 0)
		return FORTRESS_ERR_STATE;
	if (freshness == 0)
		return FORTRESS_ERR_REPLAY;
	if (fresh->slot[slot].counter == 0) {
		if (freshness > FORTRESS_FRESH_WINDOW)
			return FORTRESS_ERR_WINDOW;
	} else if (freshness <= fresh->slot[slot].counter) {
		return FORTRESS_ERR_REPLAY;
	} else if (freshness > fresh->slot[slot].counter + FORTRESS_FRESH_WINDOW) {
		return FORTRESS_ERR_WINDOW;
	}
	memcpy(aad, frame, FORTRESS_SOA_HEADER);
	iv_from(sid, mid, freshness, iv);
	rc = gcm_decrypt(key, iv, aad, FORTRESS_SOA_HEADER, frame + FORTRESS_SOA_HEADER, ct_len,
			 frame + FORTRESS_SOA_HEADER + ct_len, plain);
	if (rc != FORTRESS_OK)
		return rc;
	fresh->slot[slot].counter = freshness;
	fresh->slot[slot].present = 1;
	*plain_len = ct_len;
	if (service_id)
		*service_id = sid;
	if (method_id)
		*method_id = mid;
	return FORTRESS_OK;
}
