#include "fortress/crypto.h"
#include "fortress/status.h"
#include "fortress/trust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_write(const uint8_t *buf, size_t len, FILE *fp)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (fprintf(fp, "%02x", buf[i]) < 0)
			return -1;
	}
	return 0;
}

static int hex_read(const char *s, uint8_t *out, size_t len)
{
	size_t i;

	if (strlen(s) != len * 2)
		return -1;
	for (i = 0; i < len; i++) {
		unsigned int b;

		if (sscanf(s + 2 * i, "%02x", &b) != 1)
			return -1;
		out[i] = (uint8_t)b;
	}
	return 0;
}

static int cmd_otp_provision(int argc, char **argv)
{
	const char *pem = NULL;
	const char *state = NULL;
	fortress_otp otp;
	uint8_t pub[FORTRESS_PK_LEN];
	uint8_t hash[FORTRESS_HASH_LEN];
	FILE *fp;
	char line[128];
	int i;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--pub") && i + 1 < argc)
			pem = argv[++i];
		else if (!strcmp(argv[i], "--otp") && i + 1 < argc)
			state = argv[++i];
	}
	if (!pem || !state)
		return FORTRESS_ERR_ARG;
	memset(&otp, 0, sizeof(otp));
	fp = fopen(state, "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp) && strncmp(line, "closed=1", 8) == 0)
			otp.closed = 1;
		if (fgets(line, sizeof(line), fp) && strncmp(line, "hash=", 5) == 0) {
			line[strcspn(line, "\n")] = 0;
			if (hex_read(line + 5, otp.rotpk_hash, FORTRESS_HASH_LEN) != 0) {
				fclose(fp);
				return FORTRESS_ERR_ARG;
			}
		}
		fclose(fp);
	}
	if (fortress_p256_read_pub(pem, pub) != FORTRESS_OK)
		return FORTRESS_ERR_CRYPTO;
	if (fortress_sha256(pub, FORTRESS_PK_LEN, hash) != FORTRESS_OK)
		return FORTRESS_ERR_CRYPTO;
	if (fortress_otp_provision(&otp, hash) != FORTRESS_OK) {
		fprintf(stderr, "OTP 已锁定，拒绝写入启动公钥哈希\n");
		return FORTRESS_ERR_LOCKED;
	}
	fp = fopen(state, "w");
	if (!fp)
		return FORTRESS_ERR_ARG;
	fputs("closed=0\n", fp);
	fputs("hash=", fp);
	hex_write(otp.rotpk_hash, FORTRESS_HASH_LEN, fp);
	fputc('\n', fp);
	fclose(fp);
	printf("已写入根公钥哈希（未压缩点的 SHA-256）\n");
	return FORTRESS_OK;
}

static int cmd_otp_lock(int argc, char **argv)
{
	const char *state = NULL;
	FILE *fp;
	char hash_line[128] = "hash=\n";
	char line[128];
	int i;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--otp") && i + 1 < argc)
			state = argv[++i];
	}
	if (!state)
		return FORTRESS_ERR_ARG;
	fp = fopen(state, "r");
	if (!fp) {
		fprintf(stderr, "还没有 OTP 文件\n");
		return FORTRESS_ERR_ARG;
	}
	while (fgets(line, sizeof(line), fp)) {
		if (!strncmp(line, "hash=", 5)) {
			snprintf(hash_line, sizeof(hash_line), "%s", line);
			if (hash_line[strlen(hash_line) - 1] != '\n')
				strcat(hash_line, "\n");
		}
	}
	fclose(fp);
	fp = fopen(state, "w");
	if (!fp)
		return FORTRESS_ERR_ARG;
	fputs("closed=1\n", fp);
	fputs(hash_line, fp);
	fclose(fp);
	printf("OTP 已锁定\n");
	return FORTRESS_OK;
}

int main(int argc, char **argv)
{
	int rc = FORTRESS_ERR_ARG;

	if (argc < 2) {
		fprintf(stderr, "usage: fortress-tool otp-provision|otp-lock ...\n");
		return 1;
	}
	if (!strcmp(argv[1], "otp-provision"))
		rc = cmd_otp_provision(argc - 2, argv + 2);
	else if (!strcmp(argv[1], "otp-lock"))
		rc = cmd_otp_lock(argc - 2, argv + 2);
	else
		fprintf(stderr, "未知命令\n");
	if (rc != FORTRESS_OK && rc != FORTRESS_ERR_LOCKED)
		fprintf(stderr, "%s\n", fortress_status_str(rc));
	return rc == FORTRESS_OK ? 0 : 1;
}
