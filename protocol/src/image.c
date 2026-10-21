#include "fortress/image.h"

#include "fortress/codec.h"
#include "fortress/crypto.h"
#include "fortress/status.h"

#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>

struct fortress_download {
	int begun;
	int verified;
	uint16_t ecu_id;
	uint32_t svn;
	uint32_t kvn;
	uint32_t payload_size;
	uint32_t chunk_size;
	uint32_t chunk_count;
	uint32_t next_index;
	uint8_t payload_hash[FORTRESS_HASH_LEN];
	uint8_t *chunk_hashes;
	EVP_MD_CTX *hash;
};

fortress_download *fortress_download_new(void)
{
	return calloc(1, sizeof(fortress_download));
}

void fortress_download_free(fortress_download *dl)
{
	if (!dl)
		return;
	free(dl->chunk_hashes);
	EVP_MD_CTX_free(dl->hash);
	free(dl);
}

uint32_t fortress_download_next_index(const fortress_download *dl)
{
	return dl ? dl->next_index : 0;
}

int fortress_uds_block_kind(uint8_t counter, uint32_t next_index, int *is_retransmit)
{
	uint8_t expect_new;

	if (!is_retransmit)
		return FORTRESS_ERR_ARG;
	expect_new = (uint8_t)((next_index + 1u) & 0xffu);
	if (counter == expect_new) {
		*is_retransmit = 0;
		return FORTRESS_OK;
	}
	if (next_index > 0 && counter == (uint8_t)(next_index & 0xffu)) {
		*is_retransmit = 1;
		return FORTRESS_OK;
	}
	return FORTRESS_ERR_CHUNK_SEQ;
}

static uint32_t chunk_len_at(const fortress_download *dl, uint32_t index)
{
	uint32_t full = dl->chunk_count;
	uint32_t last;

	if (index + 1u < full)
		return dl->chunk_size;
	last = dl->payload_size - (full - 1u) * dl->chunk_size;
	return last;
}

int fortress_image_pack(uint16_t ecu_id, uint32_t svn, uint32_t kvn,
			uint32_t chunk_size, const uint8_t *payload,
			uint32_t payload_len, const char *image_pem,
			uint8_t **out, size_t *out_len)
{
	uint32_t count;
	uint32_t i;
	size_t body;
	size_t total;
	uint8_t *buf;
	uint8_t payload_hash[FORTRESS_HASH_LEN];
	int rc;

	if (!payload || !payload_len || !chunk_size || !image_pem || !out || !out_len || !kvn)
		return FORTRESS_ERR_ARG;
	count = (payload_len + chunk_size - 1u) / chunk_size;
	if (count == 0 || count > 4096u)
		return FORTRESS_ERR_ARG;
	body = FORTRESS_IMAGE_FIXED + (size_t)count * FORTRESS_HASH_LEN;
	total = body + FORTRESS_SIG_LEN;
	buf = calloc(1, total);
	if (!buf)
		return FORTRESS_ERR_CRYPTO;
	rc = fortress_sha256(payload, payload_len, payload_hash);
	if (rc != FORTRESS_OK) {
		free(buf);
		return rc;
	}
	fortress_put_u32(buf, FORTRESS_MAGIC_FIMG);
	fortress_put_u16(buf + 4, 1);
	fortress_put_u16(buf + 6, ecu_id);
	fortress_put_u32(buf + 8, svn);
	fortress_put_u32(buf + 12, kvn);
	fortress_put_u32(buf + 16, payload_len);
	fortress_put_u32(buf + 20, chunk_size);
	fortress_put_u32(buf + 24, count);
	memcpy(buf + 28, payload_hash, FORTRESS_HASH_LEN);
	for (i = 0; i < count; i++) {
		uint32_t off = i * chunk_size;
		uint32_t n = chunk_size;

		if (off + n > payload_len)
			n = payload_len - off;
		rc = fortress_sha256(payload + off, n,
				     buf + FORTRESS_IMAGE_FIXED + i * FORTRESS_HASH_LEN);
		if (rc != FORTRESS_OK) {
			free(buf);
			return rc;
		}
	}
	rc = fortress_p256_sign(image_pem, buf, body, buf + body);
	if (rc != FORTRESS_OK) {
		free(buf);
		return rc;
	}
	*out = buf;
	*out_len = total;
	return FORTRESS_OK;
}

int fortress_download_begin(fortress_download *dl, const fortress_trust *trust,
			    const uint8_t *header, size_t header_len)
{
	uint32_t count;
	uint32_t chunk_size;
	uint32_t payload_size;
	size_t body;
	int rc;

	if (!dl || !trust || !header)
		return FORTRESS_ERR_ARG;
	if (fortress_get_u32(header) != FORTRESS_MAGIC_FIMG)
		return FORTRESS_ERR_MAGIC;
	if (header_len < FORTRESS_IMAGE_FIXED + FORTRESS_SIG_LEN)
		return FORTRESS_ERR_ARG;
	count = fortress_get_u32(header + 24);
	chunk_size = fortress_get_u32(header + 20);
	payload_size = fortress_get_u32(header + 16);
	if (!count || count > 4096u || !chunk_size || !payload_size)
		return FORTRESS_ERR_ARG;
	body = FORTRESS_IMAGE_FIXED + (size_t)count * FORTRESS_HASH_LEN;
	if (header_len != body + FORTRESS_SIG_LEN)
		return FORTRESS_ERR_ARG;
	if (fortress_get_u16(header + 6) != trust->ecu_id)
		return FORTRESS_ERR_ARG;
	if (fortress_get_u32(header + 12) != trust->kvn)
		return FORTRESS_ERR_KVN;
	if (fortress_get_u32(header + 8) < trust->min_svn)
		return FORTRESS_ERR_SVN;
	rc = fortress_p256_verify(trust->image_pk, header, body, header + body);
	if (rc != FORTRESS_OK)
		return rc;
	free(dl->chunk_hashes);
	EVP_MD_CTX_free(dl->hash);
	memset(dl, 0, sizeof(*dl));
	dl->chunk_hashes = malloc((size_t)count * FORTRESS_HASH_LEN);
	dl->hash = EVP_MD_CTX_new();
	if (!dl->chunk_hashes || !dl->hash ||
	    EVP_DigestInit_ex(dl->hash, EVP_sha256(), NULL) != 1) {
		free(dl->chunk_hashes);
		EVP_MD_CTX_free(dl->hash);
		dl->chunk_hashes = NULL;
		dl->hash = NULL;
		return FORTRESS_ERR_CRYPTO;
	}
	memcpy(dl->chunk_hashes, header + FORTRESS_IMAGE_FIXED,
	       (size_t)count * FORTRESS_HASH_LEN);
	dl->ecu_id = trust->ecu_id;
	dl->svn = fortress_get_u32(header + 8);
	dl->kvn = trust->kvn;
	dl->payload_size = payload_size;
	dl->chunk_size = chunk_size;
	dl->chunk_count = count;
	dl->begun = 1;
	memcpy(dl->payload_hash, header + 28, FORTRESS_HASH_LEN);
	return FORTRESS_OK;
}

int fortress_download_chunk(fortress_download *dl, uint32_t index,
			    const uint8_t *data, uint32_t len)
{
	uint8_t got[FORTRESS_HASH_LEN];
	uint32_t expect_len;
	int retransmit = 0;
	int rc;

	if (!dl || !dl->begun || !data)
		return FORTRESS_ERR_ARG;
	if (dl->verified)
		return FORTRESS_ERR_STATE;
	if (index == dl->next_index) {
		retransmit = 0;
	} else if (dl->next_index > 0 && index + 1u == dl->next_index) {
		retransmit = 1;
	} else {
		return FORTRESS_ERR_CHUNK_SEQ;
	}
	if (index >= dl->chunk_count)
		return FORTRESS_ERR_CHUNK_SEQ;
	expect_len = chunk_len_at(dl, index);
	if (len != expect_len)
		return FORTRESS_ERR_ARG;
	rc = fortress_sha256(data, len, got);
	if (rc != FORTRESS_OK)
		return rc;
	if (memcmp(got, dl->chunk_hashes + index * FORTRESS_HASH_LEN, FORTRESS_HASH_LEN) != 0)
		return FORTRESS_ERR_CHUNK_HASH;
	if (retransmit)
		return FORTRESS_OK;
	if (EVP_DigestUpdate(dl->hash, data, len) != 1)
		return FORTRESS_ERR_CRYPTO;
	dl->next_index++;
	return FORTRESS_OK;
}

int fortress_download_verify(fortress_download *dl, const fortress_trust *trust)
{
	uint8_t got[FORTRESS_HASH_LEN];
	unsigned int n = 0;

	if (!dl || !dl->begun || !trust)
		return FORTRESS_ERR_ARG;
	if (dl->next_index != dl->chunk_count)
		return FORTRESS_ERR_STATE;
	if (dl->kvn != trust->kvn)
		return FORTRESS_ERR_KVN;
	if (dl->svn < trust->min_svn)
		return FORTRESS_ERR_SVN;
	if (EVP_DigestFinal_ex(dl->hash, got, &n) != 1 || n != FORTRESS_HASH_LEN)
		return FORTRESS_ERR_CRYPTO;
	if (memcmp(got, dl->payload_hash, FORTRESS_HASH_LEN) != 0)
		return FORTRESS_ERR_SIGNATURE;
	dl->verified = 1;
	return FORTRESS_OK;
}

int fortress_download_activate(fortress_download *dl, fortress_trust *trust)
{
	if (!dl || !trust || !dl->verified)
		return FORTRESS_ERR_STATE;
	if (dl->svn < trust->min_svn)
		return FORTRESS_ERR_SVN;
	if (dl->svn > trust->min_svn)
		trust->min_svn = dl->svn;
	return FORTRESS_OK;
}
