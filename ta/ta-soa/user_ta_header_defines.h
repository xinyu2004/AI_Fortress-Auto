#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#define TA_UUID \
	{ 0x8f7a1c2e, 0x6b4d, 0x4e91, \
	  { 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x02 } }

#define TA_FLAGS \
	(TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION | TA_FLAG_INSTANCE_KEEP_ALIVE)

#define TA_STACK_SIZE (8 * 1024)
#define TA_DATA_SIZE (32 * 1024)
#define TA_VERSION "1.0"
#define TA_DESCRIPTION "ta-soa"

#endif
