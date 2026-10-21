#include "fortress/crypto.h"
#include "fortress/image.h"
#include "fortress/status.h"
#include "fortress/trust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_bytes(const char *path, const void *buf, size_t len)
{
	FILE *fp = fopen(path, "wb");

	if (!fp || fwrite(buf, 1, len, fp) != len) {
		perror(path);
		if (fp)
			fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

static int write_c_array(FILE *fp, const char *name, const uint8_t *buf, size_t len)
{
	size_t i;

	if (fprintf(fp, "static const uint8_t %s[%zu] = {\n", name, len) < 0)
		return -1;
	for (i = 0; i < len; i++) {
		if (fprintf(fp, "0x%02x,%s", buf[i], ((i + 1) % 12) == 0 ? "\n" : " ") < 0)
			return -1;
	}
	return fputs("\n};\n", fp) < 0 ? -1 : 0;
}

int main(int argc, char **argv)
{
	const char *dir;
	char path[512];
	uint8_t root_pk[FORTRESS_PK_LEN];
	uint8_t image_pk[FORTRESS_PK_LEN];
	uint8_t next_pk[FORTRESS_PK_LEN];
	uint8_t manifest[FORTRESS_MANIFEST_LEN];
	uint8_t payload[] = "abcdefghij";
	uint8_t *image = NULL;
	uint8_t *old = NULL;
	size_t image_len = 0;
	size_t old_len = 0;
	fortress_trust trust;
	FILE *fp;
	uint8_t key[16];

	if (argc != 3) {
		fprintf(stderr, "usage: %s <fixture-dir> <trust_init.h>\n", argv[0]);
		return 1;
	}
	dir = argv[1];
	snprintf(path, sizeof(path), "%s/root.pem", dir);
	if (fortress_p256_generate(path, root_pk) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/image.pem", dir);
	if (fortress_p256_generate(path, image_pk) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/image2.pem", dir);
	if (fortress_p256_generate(path, next_pk) != FORTRESS_OK)
		return 1;
	if (fortress_trust_provision(&trust, 7, root_pk, image_pk, 1, 1) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/root.pem", dir);
	if (fortress_manifest_pack(7, 2, 0, 0, next_pk, path, manifest) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/manifest.bin", dir);
	if (write_bytes(path, manifest, sizeof(manifest)) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s/image2.pem", dir);
	if (fortress_image_pack(7, 2, 2, 4, payload, sizeof(payload) - 1, path, &image,
				&image_len) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/image.hdr", dir);
	if (write_bytes(path, image, image_len) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s/image2.pem", dir);
	if (fortress_image_pack(7, 1, 2, 4, payload, sizeof(payload) - 1, path, &old,
				&old_len) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/image_old.hdr", dir);
	if (write_bytes(path, old, old_len) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s/payload.bin", dir);
	if (write_bytes(path, payload, sizeof(payload) - 1) != 0)
		return 1;
	if (fortress_random(key, sizeof(key)) != FORTRESS_OK)
		return 1;
	snprintf(path, sizeof(path), "%s/soa.key", dir);
	if (write_bytes(path, key, sizeof(key)) != 0)
		return 1;

	fp = fopen(argv[2], "w");
	if (!fp)
		return 1;
	fputs("#ifndef TRUST_INIT_H\n#define TRUST_INIT_H\n", fp);
	fputs("#define TRUST_INIT_KVN 1u\n#define TRUST_INIT_SVN 1u\n", fp);
	if (write_c_array(fp, "trust_init_root_pk", root_pk, sizeof(root_pk)) != 0 ||
	    write_c_array(fp, "trust_init_image_pk", image_pk, sizeof(image_pk)) != 0 ||
	    write_c_array(fp, "trust_init_root_hash", trust.root_hash, sizeof(trust.root_hash)) != 0) {
		fclose(fp);
		return 1;
	}
	fputs("#endif\n", fp);
	fclose(fp);
	free(image);
	free(old);
	return 0;
}
