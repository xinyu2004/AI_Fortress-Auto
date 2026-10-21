#ifndef FORTRESS_SESSION_H
#define FORTRESS_SESSION_H

#include <tee_client_api.h>

#include <sys/types.h>

int fortress_open_ta(TEEC_Context *ctx, TEEC_Session *sess, const TEEC_UUID *uuid,
		     gid_t login_gid);

/* The process has already dropped to login_gid. Opens another session only. */
int fortress_open_session(TEEC_Context *ctx, TEEC_Session *sess, const TEEC_UUID *uuid,
			  gid_t login_gid);

#endif
