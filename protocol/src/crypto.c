#include "fortress/crypto.h"
#include "fortress/status.h"

#include <stdio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <string.h>

int fortress_sha256(const uint8_t *data, size_t len, uint8_t out[FORTRESS_HASH_LEN])
{
	EVP_MD_CTX *ctx;
	unsigned int n = 0;
	int ok;

	if (!out || (!data && len))
		return FORTRESS_ERR_ARG;
	ctx = EVP_MD_CTX_new();
	if (!ctx)
		return FORTRESS_ERR_CRYPTO;
	ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 &&
	     EVP_DigestUpdate(ctx, data, len) == 1 &&
	     EVP_DigestFinal_ex(ctx, out, &n) == 1 && n == FORTRESS_HASH_LEN;
	EVP_MD_CTX_free(ctx);
	return ok ? FORTRESS_OK : FORTRESS_ERR_CRYPTO;
}

int fortress_random(uint8_t *buf, size_t len)
{
	if (!buf || !len)
		return FORTRESS_ERR_ARG;
	return RAND_bytes(buf, (int)len) == 1 ? FORTRESS_OK : FORTRESS_ERR_CRYPTO;
}

static int der_ecdsa(const uint8_t sig[FORTRESS_SIG_LEN], uint8_t *der, size_t *der_len)
{
	const uint8_t *r = sig;
	const uint8_t *s = sig + 32;
	size_t rlen = 32;
	size_t slen = 32;
	size_t o = 0;
	size_t seq;

	while (rlen > 1 && r[0] == 0)
		r++, rlen--;
	while (slen > 1 && s[0] == 0)
		s++, slen--;
	seq = 2 + rlen + (r[0] & 0x80 ? 1 : 0) + 2 + slen + (s[0] & 0x80 ? 1 : 0);
	if (2 + seq > *der_len)
		return FORTRESS_ERR_CRYPTO;
	der[o++] = 0x30;
	der[o++] = (uint8_t)seq;
	der[o++] = 0x02;
	der[o++] = (uint8_t)(rlen + (r[0] & 0x80 ? 1 : 0));
	if (r[0] & 0x80)
		der[o++] = 0x00;
	memcpy(der + o, r, rlen);
	o += rlen;
	der[o++] = 0x02;
	der[o++] = (uint8_t)(slen + (s[0] & 0x80 ? 1 : 0));
	if (s[0] & 0x80)
		der[o++] = 0x00;
	memcpy(der + o, s, slen);
	o += slen;
	*der_len = o;
	return FORTRESS_OK;
}

static int pkey_from_pub(const uint8_t pk[FORTRESS_PK_LEN], EVP_PKEY **out)
{
	EVP_PKEY_CTX *ctx;
	OSSL_PARAM params[3];
	int rc;

	if (pk[0] != 0x04)
		return FORTRESS_ERR_ARG;
	ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
	if (!ctx || EVP_PKEY_fromdata_init(ctx) != 1) {
		EVP_PKEY_CTX_free(ctx);
		return FORTRESS_ERR_CRYPTO;
	}
	params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
						     "P-256", 0);
	params[1] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
						      (void *)pk, FORTRESS_PK_LEN);
	params[2] = OSSL_PARAM_construct_end();
	rc = EVP_PKEY_fromdata(ctx, out, EVP_PKEY_PUBLIC_KEY, params);
	EVP_PKEY_CTX_free(ctx);
	return rc == 1 ? FORTRESS_OK : FORTRESS_ERR_CRYPTO;
}

static int pub_from_pkey(EVP_PKEY *pkey, uint8_t pub[FORTRESS_PK_LEN])
{
	size_t len = FORTRESS_PK_LEN;

	if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, pub,
					    FORTRESS_PK_LEN, &len) != 1 ||
	    len != FORTRESS_PK_LEN)
		return FORTRESS_ERR_CRYPTO;
	return FORTRESS_OK;
}

static EVP_PKEY *load_private(const char *pem_path)
{
	FILE *fp;
	EVP_PKEY *pkey;

	fp = fopen(pem_path, "r");
	if (!fp)
		return NULL;
	pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
	fclose(fp);
	return pkey;
}

int fortress_p256_generate(const char *pem_path, uint8_t pub[FORTRESS_PK_LEN])
{
	EVP_PKEY_CTX *ctx;
	EVP_PKEY *pkey = NULL;
	FILE *fp;
	int rc = FORTRESS_ERR_CRYPTO;

	if (!pem_path || !pub)
		return FORTRESS_ERR_ARG;
	ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
	if (!ctx)
		return FORTRESS_ERR_CRYPTO;
	if (EVP_PKEY_keygen_init(ctx) != 1 ||
	    EVP_PKEY_CTX_set_group_name(ctx, "P-256") != 1 ||
	    EVP_PKEY_generate(ctx, &pkey) != 1)
		goto out;
	if (pub_from_pkey(pkey, pub) != FORTRESS_OK)
		goto out;
	fp = fopen(pem_path, "w");
	if (!fp)
		goto out;
	if (PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL) == 1)
		rc = FORTRESS_OK;
	fclose(fp);
out:
	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(ctx);
	return rc;
}

int fortress_p256_read_pub(const char *pem_path, uint8_t pub[FORTRESS_PK_LEN])
{
	FILE *fp;
	EVP_PKEY *pkey;
	int rc;

	if (!pem_path || !pub)
		return FORTRESS_ERR_ARG;
	fp = fopen(pem_path, "r");
	if (!fp)
		return FORTRESS_ERR_ARG;
	pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
	if (!pkey) {
		rewind(fp);
		pkey = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
	}
	fclose(fp);
	if (!pkey)
		return FORTRESS_ERR_CRYPTO;
	rc = pub_from_pkey(pkey, pub);
	EVP_PKEY_free(pkey);
	return rc;
}

int fortress_p256_sign(const char *pem_path, const uint8_t *msg, size_t msg_len,
		       uint8_t sig[FORTRESS_SIG_LEN])
{
	EVP_PKEY *pkey;
	EVP_MD_CTX *md;
	ECDSA_SIG *ec = NULL;
	uint8_t der[80];
	size_t der_len = sizeof(der);
	const unsigned char *p;
	const BIGNUM *cr;
	const BIGNUM *cs;
	int rc = FORTRESS_ERR_CRYPTO;

	if (!pem_path || !sig || (!msg && msg_len))
		return FORTRESS_ERR_ARG;
	pkey = load_private(pem_path);
	if (!pkey)
		return FORTRESS_ERR_CRYPTO;
	md = EVP_MD_CTX_new();
	if (!md)
		goto out;
	if (EVP_DigestSignInit(md, NULL, EVP_sha256(), NULL, pkey) != 1 ||
	    EVP_DigestSign(md, der, &der_len, msg, msg_len) != 1)
		goto out;
	p = der;
	ec = d2i_ECDSA_SIG(NULL, &p, (long)der_len);
	if (!ec)
		goto out;
	ECDSA_SIG_get0(ec, &cr, &cs);
	if (BN_bn2binpad(cr, sig, 32) == 32 && BN_bn2binpad(cs, sig + 32, 32) == 32)
		rc = FORTRESS_OK;
out:
	ECDSA_SIG_free(ec);
	EVP_MD_CTX_free(md);
	EVP_PKEY_free(pkey);
	return rc;
}

int fortress_p256_verify(const uint8_t pk[FORTRESS_PK_LEN],
			 const uint8_t *msg, size_t msg_len,
			 const uint8_t sig[FORTRESS_SIG_LEN])
{
	EVP_PKEY *pkey = NULL;
	EVP_MD_CTX *md = NULL;
	uint8_t der[80];
	size_t der_len = sizeof(der);
	int rc;

	if (!pk || !sig || (!msg && msg_len))
		return FORTRESS_ERR_ARG;
	rc = der_ecdsa(sig, der, &der_len);
	if (rc != FORTRESS_OK)
		return rc;
	rc = pkey_from_pub(pk, &pkey);
	if (rc != FORTRESS_OK)
		return rc;
	md = EVP_MD_CTX_new();
	if (!md) {
		rc = FORTRESS_ERR_CRYPTO;
		goto out;
	}
	if (EVP_DigestVerifyInit(md, NULL, EVP_sha256(), NULL, pkey) != 1) {
		rc = FORTRESS_ERR_CRYPTO;
		goto out;
	}
	rc = EVP_DigestVerify(md, der, der_len, msg, msg_len) == 1 ?
		     FORTRESS_OK :
		     FORTRESS_ERR_SIGNATURE;
out:
	EVP_MD_CTX_free(md);
	EVP_PKEY_free(pkey);
	return rc;
}
