#include "session.h"
#include "wire.h"

#include <fortress/ta_ids.h>
#include <fortress/trust.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const TEEC_UUID ta_ota = {
	0x8f7a1c2e, 0x6b4d, 0x4e91,
	{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x01 }
};

static const TEEC_UUID ta_soa = {
	0x8f7a1c2e, 0x6b4d, 0x4e91,
	{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x02 }
};

static int invoke(TEEC_Session *sess, uint32_t cmd, TEEC_Operation *op, const char *what)
{
	uint32_t origin = 0;
	TEEC_Result rc = TEEC_InvokeCommand(sess, cmd, op, &origin);

	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "%s failed 0x%x\n", what, rc);
		return -1;
	}
	return 0;
}

static int install_key(const char *path)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	uint8_t key[16];
	FILE *fp;
	TEEC_Operation op;

	fp = fopen(path, "rb");
	if (!fp || fread(key, 1, sizeof(key), fp) != sizeof(key)) {
		perror(path);
		if (fp)
			fclose(fp);
		return 1;
	}
	fclose(fp);
	if (fortress_open_ta(&ctx, &sess, &ta_soa, FORTRESS_GID_OTA) != 0)
		return 1;
	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = key;
	op.params[0].tmpref.size = sizeof(key);
	if (invoke(&sess, CMD_SOA_INSTALL_KEY, &op, "install key") != 0)
		return 1;
	printf("ca-ota installed the SecOC key into ta-soa\n");
	TEEC_CloseSession(&sess);
	TEEC_FinalizeContext(&ctx);
	return 0;
}

static int status(TEEC_Session *sess, uint32_t *kvn, uint32_t *svn)
{
	TEEC_Operation op;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	if (invoke(sess, CMD_GET_STATUS, &op, "status") != 0)
		return -1;
	*kvn = op.params[0].value.a;
	*svn = op.params[0].value.b;
	printf("KVN %u min SVN %u\n", *kvn, *svn);
	return 0;
}

static int send_mem(TEEC_Session *sess, uint32_t cmd, void *buf, size_t len, const char *what)
{
	TEEC_Operation op;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = buf;
	op.params[0].tmpref.size = len;
	return invoke(sess, cmd, &op, what);
}

static int send_chunk(TEEC_Session *sess, uint32_t index, void *buf, size_t len)
{
	TEEC_Operation op;
	char what[32];

	snprintf(what, sizeof(what), "chunk %u", index + 1u);
	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_MEMREF_TEMP_INPUT, TEEC_NONE,
					 TEEC_NONE);
	op.params[0].value.a = index;
	op.params[1].tmpref.buffer = buf;
	op.params[1].tmpref.size = len;
	if (invoke(sess, CMD_IMAGE_UPDATE, &op, what) != 0)
		return -1;
	printf("chunk %u accepted\n", index + 1u);
	return 0;
}

#define OTA_FRAME_MAX 8192u

static int write_full(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len) {
		ssize_t n = write(fd, p, len);

		if (n <= 0)
			return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int read_full(int fd, void *buf, size_t len)
{
	uint8_t *p = buf;

	while (len) {
		ssize_t n = read(fd, p, len);

		if (n <= 0)
			return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int reply(int fd, int ok, const void *data, uint32_t len)
{
	uint8_t hdr[5];

	hdr[0] = ok ? 1 : 0;
	hdr[1] = (uint8_t)len;
	hdr[2] = (uint8_t)(len >> 8);
	hdr[3] = (uint8_t)(len >> 16);
	hdr[4] = (uint8_t)(len >> 24);
	if (write_full(fd, hdr, sizeof(hdr)) != 0)
		return -1;
	if (len && write_full(fd, data, len) != 0)
		return -1;
	return 0;
}

static int handle_tester(TEEC_Session *sess, int fd, int *programming, int *unlocked)
{
	uint8_t hdr[7];
	uint8_t data[OTA_FRAME_MAX];
	uint8_t sid;
	uint16_t sub;
	uint32_t len;
	int answered = 0;
	int replied = 0;

	if (read_full(fd, hdr, sizeof(hdr)) != 0)
		return -1;
	sid = hdr[0];
	sub = (uint16_t)hdr[1] | ((uint16_t)hdr[2] << 8);
	len = (uint32_t)hdr[3] | ((uint32_t)hdr[4] << 8) | ((uint32_t)hdr[5] << 16) |
	      ((uint32_t)hdr[6] << 24);
	if (len > OTA_FRAME_MAX) {
		reply(fd, 0, NULL, 0);
		return -1;
	}
	if (len && read_full(fd, data, len) != 0)
		return -1;
	if (sid != 0x10 && !*programming) {
		fprintf(stderr, "programming session is not open\n");
		reply(fd, 0, NULL, 0);
		return -1;
	}
	if ((sid == 0x31 || sid == 0x34 || sid == 0x36 || sid == 0x37) && !*unlocked) {
		fprintf(stderr, "security access is not open\n");
		reply(fd, 0, NULL, 0);
		return -1;
	}
	switch (sid) {
	case 0x10:
		if (sub != 0x02 || len != 0)
			break;
		*programming = 1;
		printf("programming session open\n");
		answered = 1;
		break;
	case 0x27:
		if (sub == 0x01 && len == 0) {
			uint8_t seed[32];
			TEEC_Operation op;

			memset(&op, 0, sizeof(op));
			op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE,
							 TEEC_NONE);
			op.params[0].tmpref.buffer = seed;
			op.params[0].tmpref.size = sizeof(seed);
			if (invoke(sess, CMD_GET_SEED, &op, "0x27 01") != 0)
				break;
			if (op.params[0].tmpref.size != sizeof(seed))
				break;
			*unlocked = 0;
			printf("seed issued\n");
			if (reply(fd, 1, seed, sizeof(seed)) != 0)
				return -1;
			answered = 1;
			replied = 1;
		} else if (sub == 0x02 && len == FORTRESS_SIG_LEN) {
			if (send_mem(sess, CMD_UNLOCK, data, len, "0x27 02") != 0)
				break;
			*unlocked = 1;
			printf("security access unlocked\n");
			answered = 1;
		}
		break;
	case 0x22: {
		uint32_t kvn = 0;
		uint32_t svn = 0;
		uint8_t body[8];

		if (len != 0 || status(sess, &kvn, &svn) != 0)
			break;
		body[0] = (uint8_t)kvn;
		body[1] = (uint8_t)(kvn >> 8);
		body[2] = (uint8_t)(kvn >> 16);
		body[3] = (uint8_t)(kvn >> 24);
		body[4] = (uint8_t)svn;
		body[5] = (uint8_t)(svn >> 8);
		body[6] = (uint8_t)(svn >> 16);
		body[7] = (uint8_t)(svn >> 24);
		if (reply(fd, 1, body, sizeof(body)) != 0)
			return -1;
		answered = 1;
		replied = 1;
		break;
	}
	case 0x31:
		if (sub == 0xF000) {
			if (send_mem(sess, CMD_UPDATE_KEY_MANIFEST, data, len, "0x31 F000") != 0)
				break;
			printf("manifest accepted, image public key replaced\n");
			answered = 1;
		} else if (sub == 0xF002 && len == 0) {
			TEEC_Operation op;

			memset(&op, 0, sizeof(op));
			op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
			if (invoke(sess, CMD_IMAGE_ACTIVATE, &op, "0x31 F002") != 0)
				break;
			printf("min SVN written\n");
			answered = 1;
		}
		break;
	case 0x34:
		if (send_mem(sess, CMD_IMAGE_BEGIN, data, len, "0x34") != 0)
			break;
		printf("image header accepted\n");
		answered = 1;
		break;
	case 0x36:
		if (len < 2 || data[0] == 0)
			break;
		if (send_chunk(sess, (uint32_t)data[0] - 1u, data + 1, len - 1u) != 0)
			break;
		answered = 1;
		break;
	case 0x37:
		if (len != 0) {
			break;
		} else {
			TEEC_Operation op;

			memset(&op, 0, sizeof(op));
			op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
			if (invoke(sess, CMD_IMAGE_VERIFY, &op, "0x37") != 0)
				break;
			printf("package signature and chunk hashes verified\n");
			answered = 1;
		}
		break;
	default:
		break;
	}
	if (!answered) {
		reply(fd, 0, NULL, 0);
		fflush(stdout);
		return -1;
	}
	if (!replied && reply(fd, 1, NULL, 0) != 0)
		return -1;
	fflush(stdout);
	return 0;
}

static int listen_tester(void)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	int srv;
	int one = 1;
	struct sockaddr_in addr;

	if (fortress_open_ta(&ctx, &sess, &ta_ota, FORTRESS_GID_OTA) != 0)
		return 1;
	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) {
		perror("socket");
		return 1;
	}
	if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
		perror("setsockopt");
		return 1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(FORTRESS_OTA_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(srv, 1) != 0) {
		perror("listen");
		return 1;
	}
	printf("ca-ota waiting for the diagnostic tester on tcp port %u\n", FORTRESS_OTA_PORT);
	printf("KVN and min SVN are stored by tee-supplicant. This is not RPMB.\n");
	fflush(stdout);
	for (;;) {
		int fd = accept(srv, NULL, NULL);
		int programming = 0;
		int unlocked = 0;

		if (fd < 0)
			continue;
		while (handle_tester(&sess, fd, &programming, &unlocked) == 0)
			;
		close(fd);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "install-key"))
		return install_key(argv[2]);
	if (argc == 2 && !strcmp(argv[1], "listen"))
		return listen_tester();
	fprintf(stderr, "usage: %s install-key <file> | listen\n", argv[0]);
	return 1;
}
