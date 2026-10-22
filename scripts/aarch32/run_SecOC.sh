#!/bin/bash
# Linux console for the SecOC entry. The OP-TEE window is scripts/aarch32/run_tee.sh.
exec "$(cd "$(dirname "$0")" && pwd)/run_linux.sh" SecOC "$@"
