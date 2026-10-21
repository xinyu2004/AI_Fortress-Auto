#ifndef FORTRESS_SECOC_IO_H
#define FORTRESS_SECOC_IO_H

#include "session.h"

#include <stddef.h>
#include <stdint.h>

#define SOCK_PERCEPTION "/tmp/fortress-perception.sock"
#define SOCK_PLANNING "/tmp/fortress-planning.sock"

int secoc_bind(const char *path);
int secoc_send(int fd, const char *path, const uint8_t *buf, size_t len);
int secoc_seal(TEEC_Session *sess, uint16_t sid, const char *text, uint8_t *frame,
	       size_t *frame_len);
int secoc_open_frame(TEEC_Session *sess, const uint8_t *frame, size_t frame_len, char *plain,
		     size_t plain_cap);

#endif
