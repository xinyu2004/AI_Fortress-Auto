#include "session.h"

#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int fortress_open_ta(TEEC_Context *ctx, TEEC_Session *sess, const TEEC_UUID *uuid,
		     gid_t login_gid)
{
	struct group *teeclnt;
	gid_t groups[2];
	int ngroups = 0;

	teeclnt = getgrnam("teeclnt");
	if (teeclnt)
		groups[ngroups++] = teeclnt->gr_gid;
	groups[ngroups++] = login_gid;
	if (setgroups(ngroups, groups) != 0 || setgid(login_gid) != 0 ||
	    setuid(login_gid) != 0) {
		perror("drop privileges");
		return -1;
	}
	return fortress_open_session(ctx, sess, uuid, login_gid);
}

int fortress_open_session(TEEC_Context *ctx, TEEC_Session *sess, const TEEC_UUID *uuid,
			  gid_t login_gid)
{
	TEEC_Result rc;
	uint32_t origin = 0;
	gid_t gid = login_gid;

	rc = TEEC_InitializeContext(NULL, ctx);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InitializeContext 0x%x\n", rc);
		return -1;
	}
	rc = TEEC_OpenSession(ctx, sess, uuid, TEEC_LOGIN_GROUP, &gid, NULL, &origin);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_OpenSession 0x%x origin 0x%x\n", rc, origin);
		TEEC_FinalizeContext(ctx);
		return -1;
	}
	return 0;
}
