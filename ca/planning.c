#include "secoc_io.h"
#include "wire.h"

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

static const TEEC_UUID ta_soa = {
	0x8f7a1c2e, 0x6b4d, 0x4e91,
	{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x02 }
};

int main(void)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	int fd;
	unsigned n = 1;

	printf("planning: reply on each perception frame\n");
	if (fortress_open_ta(&ctx, &sess, &ta_soa, FORTRESS_GID_PLANNING) != 0)
		return 1;
	fd = secoc_bind(SOCK_PLANNING);
	if (fd < 0) {
		perror("bind planning");
		return 1;
	}
	for (;;) {
		uint8_t frame[256];
		char plain[128];
		char reply_text[64];
		uint8_t reply[256];
		size_t reply_len = 0;
		ssize_t got = recv(fd, frame, sizeof(frame), 0);

		if (got < 0) {
			perror("recv");
			return 1;
		}
		if (secoc_open_frame(&sess, frame, (size_t)got, plain, sizeof(plain)) != 0) {
			fprintf(stderr, "planning failed to open the perception frame\n");
			return 1;
		}
		printf("planning receive check %s\n", plain);
		snprintf(reply_text, sizeof(reply_text), "slow-down-%u", n);
		if (secoc_seal(&sess, FORTRESS_SID_PLANNING, reply_text, reply, &reply_len) != 0)
			return 1;
		printf("planning send %s\n", reply_text);
		if (secoc_send(fd, SOCK_PERCEPTION, reply, reply_len) != 0) {
			perror("reply");
			return 1;
		}
		n++;
	}
}
