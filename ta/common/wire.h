#ifndef FORTRESS_WIRE_H
#define FORTRESS_WIRE_H

#include <stdint.h>

/* Pinned in the guest /etc/group. teeclnt stays dynamic and is looked up by name. */
#define FORTRESS_GID_OTA 2000u
#define FORTRESS_GID_PERCEPTION 2001u
#define FORTRESS_GID_PLANNING 2002u

#define FORTRESS_SID_PERCEPTION 1u
#define FORTRESS_SID_PLANNING 2u

#define FORTRESS_ECU_ID 7u

/* QEMU user-net hostfwd. ca-ota listens; scripts/diag_tester.sh connects. */
#define FORTRESS_OTA_PORT 27100u

/* SEAL input: service, method, plain_len, plain. Output: the frame. */
/* OPEN input: the frame. Output: service, method, plain_len, plain. */

#endif
