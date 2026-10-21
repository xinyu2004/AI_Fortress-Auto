#ifndef FORTRESS_IMAGE_H
#define FORTRESS_IMAGE_H

#include "fortress/trust.h"

#include <stddef.h>
#include <stdint.h>

#define FORTRESS_MAGIC_FIMG 0x474D4946u /* 'FIMG' */
#define FORTRESS_IMAGE_FIXED 60

typedef struct fortress_download fortress_download;

fortress_download *fortress_download_new(void);
void fortress_download_free(fortress_download *dl);

int fortress_image_pack(uint16_t ecu_id, uint32_t svn, uint32_t kvn,
			uint32_t chunk_size, const uint8_t *payload,
			uint32_t payload_len, const char *image_pem,
			uint8_t **out, size_t *out_len);

int fortress_download_begin(fortress_download *dl, const fortress_trust *trust,
			    const uint8_t *header, size_t header_len);

/* index 从 0 起。重复上一块视为重传，不推进序号。 */
int fortress_download_chunk(fortress_download *dl, uint32_t index,
			    const uint8_t *data, uint32_t len);

int fortress_download_verify(fortress_download *dl, const fortress_trust *trust);
int fortress_download_activate(fortress_download *dl, fortress_trust *trust);

uint32_t fortress_download_next_index(const fortress_download *dl);

/* UDS blockSequenceCounter：首块为 0x01，0xFF 之后绕回 0x00。 */
int fortress_uds_block_kind(uint8_t counter, uint32_t next_index,
			    int *is_retransmit);

#endif
