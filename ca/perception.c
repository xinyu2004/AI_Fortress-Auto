#include "secoc_io.h"
#include "wire.h"

#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static const TEEC_UUID ta_soa = {
	0x8f7a1c2e, 0x6b4d, 0x4e91,
	{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x02 }
};

static void pace_100ms(const struct timespec *t0)
{
	struct timespec t1;
	long us;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	us = (t1.tv_sec - t0->tv_sec) * 1000000L + (t1.tv_nsec - t0->tv_nsec) / 1000L;
	if (us >= 0 && us < 100000)
		usleep((useconds_t)(100000 - us));
}

int main(void)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	int fd;
	unsigned n = 1;

	printf("perception: 100 ms, plaintext changes each frame\n");
	if (fortress_open_ta(&ctx, &sess, &ta_soa, FORTRESS_GID_PERCEPTION) != 0)
		return 1;
	fd = secoc_bind(SOCK_PERCEPTION);
	if (fd < 0) {
		perror("bind perception");
		return 1;
	}
	for (;;) {
		struct timespec t0;
		char text[64];
		char plain[128];
		uint8_t frame[256];
		size_t frame_len = 0;
		ssize_t got;

		clock_gettime(CLOCK_MONOTONIC, &t0);
		snprintf(text, sizeof(text), "front-objects-%u", n);
		if (secoc_seal(&sess, FORTRESS_SID_PERCEPTION, text, frame, &frame_len) != 0)
			return 1;
		printf("\nperception send %s\n", text);
		if (secoc_send(fd, SOCK_PLANNING, frame, frame_len) != 0) {
			perror("send");
			return 1;
		}
		got = recv(fd, frame, sizeof(frame), 0);
		if (got < 0) {
			perror("recv");
			return 1;
		}
		if (secoc_open_frame(&sess, frame, (size_t)got, plain, sizeof(plain)) != 0) {
			fprintf(stderr, "perception failed to open the planning reply\n");
			return 1;
		}
		printf("perception receive check %s\n", plain);
		n++;
		pace_100ms(&t0);
	}
}
