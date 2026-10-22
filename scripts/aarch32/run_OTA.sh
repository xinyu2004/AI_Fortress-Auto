#!/bin/bash
# Linux console for the AArch32 OTA entry. The OP-TEE window is scripts/aarch32/run_tee.sh.
exec "$(cd "$(dirname "$0")" && pwd)/run_linux.sh" OTA "$@"
