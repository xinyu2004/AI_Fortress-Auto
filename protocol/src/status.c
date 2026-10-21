#include "fortress/status.h"

const char *fortress_status_str(int status)
{
	switch (status) {
	case FORTRESS_OK:
		return "ok";
	case FORTRESS_ERR_ARG:
		return "bad argument";
	case FORTRESS_ERR_MAGIC:
		return "bad magic";
	case FORTRESS_ERR_SIGNATURE:
		return "signature rejected";
	case FORTRESS_ERR_KVN:
		return "key version rejected";
	case FORTRESS_ERR_SVN:
		return "security version rejected";
	case FORTRESS_ERR_CHUNK_HASH:
		return "chunk hash mismatch";
	case FORTRESS_ERR_CHUNK_SEQ:
		return "chunk sequence rejected";
	case FORTRESS_ERR_LOCKED:
		return "otp locked";
	case FORTRESS_ERR_REPLAY:
		return "replay rejected";
	case FORTRESS_ERR_WINDOW:
		return "freshness outside window";
	case FORTRESS_ERR_STATE:
		return "invalid state";
	case FORTRESS_ERR_CRYPTO:
		return "crypto failed";
	default:
		return "unknown";
	}
}
