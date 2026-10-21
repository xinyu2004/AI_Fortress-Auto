#ifndef FORTRESS_SOA_H
#define FORTRESS_SOA_H

#include <stddef.h>
#include <stdint.h>

#define FORTRESS_AES_LEN 16
#define FORTRESS_GCM_TAG_LEN 16
#define FORTRESS_SOA_HEADER 10
#define FORTRESS_FRESH_SLOTS 16
#define FORTRESS_FRESH_WINDOW 8u

typedef struct fortress_fresh_table {
	struct {
		uint16_t service_id;
		uint32_t counter;
		int present;
	} slot[FORTRESS_FRESH_SLOTS];
} fortress_fresh_table;

/* 帧：service u16、method u16、freshness u32、ciphertext、tag[16]。 */
int fortress_soa_seal(fortress_fresh_table *fresh, uint16_t service_id,
		      uint16_t method_id, const uint8_t key[FORTRESS_AES_LEN],
		      const uint8_t *plain, uint16_t plain_len,
		      uint8_t *out, size_t out_cap, size_t *out_len);

int fortress_soa_open(fortress_fresh_table *fresh,
		      const uint8_t key[FORTRESS_AES_LEN],
		      const uint8_t *frame, size_t frame_len,
		      uint8_t *plain, size_t plain_cap, size_t *plain_len,
		      uint16_t *service_id, uint16_t *method_id);

#endif
