#include "fortress/crypto.h"
#include "fortress/image.h"
#include "fortress/soa.h"
#include "fortress/status.h"
#include "fortress/trust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_failed;

static void expect_eq(const char *name, int got, int want)
{
	if (got == want)
		return;
	fprintf(stderr, "FAIL %s: got %s (%d), want %s (%d)\n", name,
		fortress_status_str(got), got, fortress_status_str(want), want);
	g_failed++;
}

static void expect_true(const char *name, int cond)
{
	if (cond)
		return;
	fprintf(stderr, "FAIL %s\n", name);
	g_failed++;
}

static void test_uds(void)
{
	int replay = 0;

	expect_eq("uds first", fortress_uds_block_kind(1, 0, &replay), FORTRESS_OK);
	expect_true("uds first is new", replay == 0);
	expect_eq("uds skip", fortress_uds_block_kind(3, 0, &replay), FORTRESS_ERR_CHUNK_SEQ);
	expect_eq("uds wrap new", fortress_uds_block_kind(0, 255, &replay), FORTRESS_OK);
	expect_true("wrap is new", replay == 0);
	expect_eq("uds wrap retransmit", fortress_uds_block_kind(255, 255, &replay), FORTRESS_OK);
	expect_true("wrap retransmit", replay == 1);
	expect_eq("uds after wrap", fortress_uds_block_kind(1, 256, &replay), FORTRESS_OK);
	expect_true("after wrap is new", replay == 0);
}

static void test_policy(const char *dir)
{
	char root_pem[256], image_pem[256], next_pem[256];
	uint8_t root_pk[FORTRESS_PK_LEN], image_pk[FORTRESS_PK_LEN], next_pk[FORTRESS_PK_LEN];
	uint8_t manifest[FORTRESS_MANIFEST_LEN];
	uint8_t payload[] = "abcdefghij";
	uint8_t *image = NULL;
	size_t image_len = 0;
	fortress_trust trust;
	fortress_otp otp;
	fortress_download *dl;
	uint32_t fixed;
	int rc;

	snprintf(root_pem, sizeof(root_pem), "%s/root.pem", dir);
	snprintf(image_pem, sizeof(image_pem), "%s/image.pem", dir);
	snprintf(next_pem, sizeof(next_pem), "%s/image2.pem", dir);
	expect_eq("gen root", fortress_p256_generate(root_pem, root_pk), FORTRESS_OK);
	expect_eq("gen image", fortress_p256_generate(image_pem, image_pk), FORTRESS_OK);
	expect_eq("gen image2", fortress_p256_generate(next_pem, next_pk), FORTRESS_OK);
	expect_eq("provision",
		  fortress_trust_provision(&trust, 7, root_pk, image_pk, 1, 1),
		  FORTRESS_OK);

	memset(&otp, 0, sizeof(otp));
	expect_eq("otp write", fortress_otp_provision(&otp, trust.root_hash), FORTRESS_OK);
	expect_eq("otp lock", fortress_otp_lock(&otp), FORTRESS_OK);
	expect_eq("otp locked", fortress_otp_provision(&otp, trust.root_hash),
		  FORTRESS_ERR_LOCKED);

	expect_eq("manifest same kvn",
		  fortress_manifest_pack(7, 1, 0, 0, image_pk, root_pem, manifest),
		  FORTRESS_OK);
	expect_eq("reject same kvn", fortress_manifest_apply(&trust, manifest, 0),
		  FORTRESS_ERR_KVN);

	expect_eq("manifest kvn2",
		  fortress_manifest_pack(7, 2, 0, 0, next_pk, root_pem, manifest),
		  FORTRESS_OK);
	expect_eq("apply kvn2", fortress_manifest_apply(&trust, manifest, 0), FORTRESS_OK);
	expect_true("kvn is 2", trust.kvn == 2);
	expect_true("image key replaced", memcmp(trust.image_pk, next_pk, FORTRESS_PK_LEN) == 0);
	expect_eq("replay manifest", fortress_manifest_apply(&trust, manifest, 0),
		  FORTRESS_ERR_KVN);

	expect_eq("pack old key",
		  fortress_image_pack(7, 2, 2, 4, payload, sizeof(payload) - 1, image_pem,
				      &image, &image_len),
		  FORTRESS_OK);
	dl = fortress_download_new();
	expect_eq("old key rejected",
		  fortress_download_begin(dl, &trust, image, image_len),
		  FORTRESS_ERR_SIGNATURE);
	free(image);
	fortress_download_free(dl);

	expect_eq("pack new key",
		  fortress_image_pack(7, 2, 2, 4, payload, sizeof(payload) - 1, next_pem,
				      &image, &image_len),
		  FORTRESS_OK);
	fixed = FORTRESS_IMAGE_FIXED + 3 * FORTRESS_HASH_LEN;
	expect_true("three chunks", image_len == fixed + FORTRESS_SIG_LEN);
	dl = fortress_download_new();
	expect_eq("begin", fortress_download_begin(dl, &trust, image, fixed + FORTRESS_SIG_LEN),
		  FORTRESS_OK);
	{
		uint8_t bad[4] = { 'X', 'b', 'c', 'd' };
		expect_eq("bad chunk", fortress_download_chunk(dl, 0, bad, 4),
			  FORTRESS_ERR_CHUNK_HASH);
		expect_true("index stays", fortress_download_next_index(dl) == 0);
	}
	expect_eq("chunk0", fortress_download_chunk(dl, 0, payload, 4), FORTRESS_OK);
	expect_eq("retransmit", fortress_download_chunk(dl, 0, payload, 4), FORTRESS_OK);
	expect_true("index is 1", fortress_download_next_index(dl) == 1);
	expect_eq("skip", fortress_download_chunk(dl, 2, payload + 8, 2), FORTRESS_ERR_CHUNK_SEQ);
	expect_eq("chunk1", fortress_download_chunk(dl, 1, payload + 4, 4), FORTRESS_OK);
	expect_eq("activate early", fortress_download_activate(dl, &trust), FORTRESS_ERR_STATE);
	expect_true("svn untouched", trust.min_svn == 1);
	expect_eq("chunk2", fortress_download_chunk(dl, 2, payload + 8, 2), FORTRESS_OK);
	expect_eq("verify", fortress_download_verify(dl, &trust), FORTRESS_OK);
	expect_true("svn still 1 before activate", trust.min_svn == 1);
	expect_eq("activate", fortress_download_activate(dl, &trust), FORTRESS_OK);
	expect_true("svn is 2", trust.min_svn == 2);
	fortress_download_free(dl);
	free(image);

	expect_eq("pack svn0",
		  fortress_image_pack(7, 0, 2, 4, payload, 4, next_pem, &image, &image_len),
		  FORTRESS_OK);
	dl = fortress_download_new();
	expect_eq("svn rejected", fortress_download_begin(dl, &trust, image, image_len),
		  FORTRESS_ERR_SVN);
	fortress_download_free(dl);
	free(image);

	expect_eq("pack same svn",
		  fortress_image_pack(7, 2, 2, 4, payload, 4, next_pem, &image, &image_len),
		  FORTRESS_OK);
	dl = fortress_download_new();
	expect_eq("begin same", fortress_download_begin(dl, &trust, image, image_len), FORTRESS_OK);
	expect_eq("only chunk", fortress_download_chunk(dl, 0, payload, 4), FORTRESS_OK);
	expect_eq("verify same", fortress_download_verify(dl, &trust), FORTRESS_OK);
	expect_eq("activate same", fortress_download_activate(dl, &trust), FORTRESS_OK);
	expect_true("svn stays 2", trust.min_svn == 2);
	fortress_download_free(dl);
	free(image);
	(void)rc;
}

static void test_soa(void)
{
	fortress_fresh_table tx;
	fortress_fresh_table rx;
	fortress_fresh_table back;
	uint8_t key[FORTRESS_AES_LEN];
	uint8_t frame[128];
	uint8_t plain[64];
	size_t frame_len = 0;
	size_t plain_len = 0;
	uint16_t sid = 0;
	uint16_t mid = 0;
	const char *msg = "objects";
	const char *intent = "intent";

	memset(&tx, 0, sizeof(tx));
	memset(&rx, 0, sizeof(rx));
	memset(&back, 0, sizeof(back));
	expect_eq("random key", fortress_random(key, sizeof(key)), FORTRESS_OK);
	expect_eq("seal",
		  fortress_soa_seal(&tx, 1, 2, key, (const uint8_t *)msg, (uint16_t)strlen(msg),
				    frame, sizeof(frame), &frame_len),
		  FORTRESS_OK);
	expect_eq("open",
		  fortress_soa_open(&rx, key, frame, frame_len, plain, sizeof(plain), &plain_len,
				    &sid, &mid),
		  FORTRESS_OK);
	expect_true("plain", plain_len == strlen(msg) && memcmp(plain, msg, plain_len) == 0);
	expect_true("ids", sid == 1 && mid == 2);
	expect_eq("replay",
		  fortress_soa_open(&rx, key, frame, frame_len, plain, sizeof(plain), &plain_len,
				    &sid, &mid),
		  FORTRESS_ERR_REPLAY);
	expect_eq("seal back",
		  fortress_soa_seal(&back, 2, 1, key, (const uint8_t *)intent,
				    (uint16_t)strlen(intent), frame, sizeof(frame), &frame_len),
		  FORTRESS_OK);
	expect_eq("open back",
		  fortress_soa_open(&tx, key, frame, frame_len, plain, sizeof(plain), &plain_len,
				    &sid, &mid),
		  FORTRESS_OK);
	expect_true("intent", plain_len == strlen(intent) && memcmp(plain, intent, plain_len) == 0);
}

int main(void)
{
	char dir[] = "/tmp/fortress-test-XXXXXX";

	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	test_uds();
	test_policy(dir);
	test_soa();
	if (g_failed) {
		fprintf(stderr, "%d failure(s)\n", g_failed);
		return 1;
	}
	printf("protocol tests passed\n");
	return 0;
}
