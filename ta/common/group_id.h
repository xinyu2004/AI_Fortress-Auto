#ifndef FORTRESS_GROUP_ID_H
#define FORTRESS_GROUP_ID_H

#include <stdint.h>
#include <tee_internal_api.h>

/* UUIDv5 of "gid=%x" in the kernel TEE namespace. Matches tee_session_calc_client_uuid(). */
TEE_Result fortress_group_uuid(uint32_t gid, TEE_UUID *out);
int fortress_client_is_gid(uint32_t gid);

#endif
