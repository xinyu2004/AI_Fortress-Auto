#ifndef FORTRESS_TA_IDS_H
#define FORTRESS_TA_IDS_H

#include <stdint.h>

/* ta-ota：和 ca-ota 通信。密钥清单、镜像、KVN / SVN */
#define TA_OTA_UUID_WORDS \
	0x8f7a1c2e, 0x6b4d, 0x4e91, \
	{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x01 }

#define CMD_GET_STATUS 0x1001u
#define CMD_GET_SEED 0x1002u
#define CMD_UNLOCK 0x1003u
#define CMD_UPDATE_KEY_MANIFEST 0x1004u
#define CMD_IMAGE_BEGIN 0x1010u
#define CMD_IMAGE_UPDATE 0x1011u
#define CMD_IMAGE_VERIFY 0x1012u
#define CMD_IMAGE_ACTIVATE 0x1013u

/* ta-soa：感知与规控 */
#define TA_SOA_UUID_WORDS \
	0x8f7a1c2e, 0x6b4d, 0x4e91, \
	{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x02 }

#define CMD_SOA_SEAL 0x2001u
#define CMD_SOA_OPEN 0x2002u
#define CMD_SOA_INSTALL_KEY 0x2003u

#define FORTRESS_UDS_ROUTINE_UPDATE_KEY 0xF000u
#define FORTRESS_UDS_ROUTINE_ACTIVATE 0xF002u
#define FORTRESS_UDS_NRC_SEQUENCE 0x24u

#endif
