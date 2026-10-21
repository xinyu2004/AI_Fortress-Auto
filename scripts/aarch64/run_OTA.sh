#!/bin/bash
# Linux console for the OTA entry. The OP-TEE window is scripts/aarch64/run_tee.sh.
exec "$(cd "$(dirname "$0")" && pwd)/run_linux.sh" OTA
