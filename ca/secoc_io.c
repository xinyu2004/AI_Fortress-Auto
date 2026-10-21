#include "secoc_io.h"
#include "wire.h"

#include <fortress/ta_ids.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int secoc_bind(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	unlink(path);
	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	/* The peer is a different user and must be able to send here. */
	if (chmod(path, 0666) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int secoc_send(int fd, const char *path, const uint8_t *buf, size_t len)
{
	struct sockaddr_un addr;
	int i;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	for (i = 0; i < 50; i++) {
		if (sendto(fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)) == (ssize_t)len)
			return 0;
		if (errno != ENOENT && errno != ECONNREFUSED && errno != EAGAIN)
			return -1;
		usleep(20000);
	}
	return -1;
}

int secoc_seal(TEEC_Session *sess, uint16_t sid, const char *text, uint8_t *frame,
	       size_t *frame_len)
{
	uint8_t in[256];
	uint16_t n = (uint16_t)strlen(text);
	TEEC_Operation op;
	uint32_t origin = 0;
	TEEC_Result rc;

	if (n > sizeof(in) - 6)
		return -1;
	in[0] = (uint8_t)sid;
	in[1] = (uint8_t)(sid >> 8);
	in[2] = 1;
	in[3] = 0;
	in[4] = (uint8_t)n;
	in[5] = (uint8_t)(n >> 8);
	memcpy(in + 6, text, n);
	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = in;
	op.params[0].tmpref.size = 6u + n;
	op.params[1].tmpref.buffer = frame;
	op.params[1].tmpref.size = 256;
	rc = TEEC_InvokeCommand(sess, CMD_SOA_SEAL, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "SEAL failed 0x%x\n", rc);
		return -1;
	}
	*frame_len = op.params[1].tmpref.size;
	return 0;
}

int secoc_open_frame(TEEC_Session *sess, const uint8_t *frame, size_t frame_len, char *plain,
		     size_t plain_cap)
{
	uint8_t out[256];
	TEEC_Operation op;
	uint32_t origin = 0;
	TEEC_Result rc;
	uint16_t n;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = (void *)frame;
	op.params[0].tmpref.size = frame_len;
	op.params[1].tmpref.buffer = out;
	op.params[1].tmpref.size = sizeof(out);
	rc = TEEC_InvokeCommand(sess, CMD_SOA_OPEN, &op, &origin);
	if (rc != TEEC_SUCCESS)
		return -1;
	if (op.params[1].tmpref.size < 6)
		return -1;
	n = (uint16_t)out[4] | ((uint16_t)out[5] << 8);
	if (6u + n > op.params[1].tmpref.size || n >= plain_cap)
		return -1;
	memcpy(plain, out + 6, n);
	plain[n] = 0;
	return 0;
}
