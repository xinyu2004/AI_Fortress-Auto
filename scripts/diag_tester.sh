#!/bin/bash
# Diagnostic tester for the OTA guest. The guest must already be waiting.
#   scripts/diag_tester.sh            with scripts/aarch64/run_OTA.sh
#   scripts/diag_tester.sh aarch32    with scripts/aarch32/run_OTA.sh
# SecOC starts from run_SecOC.sh and does not use this tester.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ARCH="${1:-aarch64}"
FIX="$ROOT/dep-src/out/$ARCH/guest-fix"
PORT=$(sed -n 's/^#define FORTRESS_OTA_PORT \([0-9]*\)u/\1/p' "$ROOT/ta/common/wire.h")

case "$ARCH" in
aarch64|aarch32) ;;
*)
	echo "usage: scripts/diag_tester.sh [aarch64|aarch32]"
	exit 1
	;;
esac

if [[ -z "$PORT" || ! -f "$FIX/manifest.bin" || ! -f "$FIX/image.hdr" || ! -f "$FIX/payload.bin" ||
	! -f "$FIX/image.pem" || ! -f "$FIX/soa.key" ]]; then
	echo "Tester files are missing. Build the guest with scripts/$ARCH/build_qemu.sh"
	exit 1
fi

exec python3 - "$PORT" "$FIX" "$ARCH" <<'PY'
import pathlib
import socket
import struct
import subprocess
import sys
import time

port = int(sys.argv[1])
fix = pathlib.Path(sys.argv[2])
arch = sys.argv[3]
deadline = time.time() + 60
sock = None
while time.time() < deadline:
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        sock.settimeout(30)
        break
    except OSError:
        time.sleep(0.2)
if sock is None:
    sys.stderr.write(
        "ca-ota is not listening on 127.0.0.1:%d.\n"
        "Start scripts/%s/run_OTA.sh and scripts/%s/run_tee.sh first.\n" % (port, arch, arch)
    )
    sys.exit(1)

def recvn(n):
    buf = b""
    while len(buf) < n:
        got = sock.recv(n - len(buf))
        if not got:
            raise SystemExit("ca-ota closed the connection")
        buf += got
    return buf

def send(label, sid, sub, data=b"", size=None):
    shown = len(data) if size is None else size
    if shown:
        print("send %s (%u bytes)" % (label, shown))
    else:
        print("send %s" % label)
    sock.sendall(struct.pack("<BHI", sid, sub, len(data)) + data)
    hdr = recvn(5)
    ok, n = struct.unpack("<BI", hdr)
    body = recvn(n) if n else b""
    if not ok:
        print("negative response")
        raise SystemExit(1)
    print("positive response")
    return body

def rd32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")

def der_rs(der):
    i = 2
    if der[0] != 0x30 or der[i] != 0x02:
        raise SystemExit("seed signature")
    i += 1
    rl = der[i]
    i += 1
    r = der[i:i + rl]
    i += rl
    if der[i] != 0x02:
        raise SystemExit("seed signature")
    i += 1
    sl = der[i]
    i += 1
    s = der[i:i + sl]
    if r[:1] == b"\x00":
        r = r[1:]
    if s[:1] == b"\x00":
        s = s[1:]
    return r.rjust(32, b"\x00") + s.rjust(32, b"\x00")

def sign_seed(seed):
    proc = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(fix / "image.pem")],
        input=seed, capture_output=True, check=True)
    return der_rs(proc.stdout)

def unlock():
    seed = send("0x27 01", 0x27, 0x01)
    if len(seed) != 32:
        raise SystemExit("seed length")
    send("0x27 02", 0x27, 0x02, sign_seed(seed))

print("diagnostic tester")
send("0x10 02", 0x10, 0x02)
unlock()
send("0x22", 0x22, 0)
manifest = (fix / "manifest.bin").read_bytes()
send("0x31 F000", 0x31, 0xF000, manifest)
header = (fix / "image.hdr").read_bytes()
payload = (fix / "payload.bin").read_bytes()
send("0x34", 0x34, 0, header)
payload_len = rd32(header, 16)
chunk_size = rd32(header, 20)
count = rd32(header, 24)
off = 0
for i in range(count):
    n = payload_len - off if i + 1 == count else chunk_size
    chunk = payload[off:off + n]
    send("0x36 block %u" % (i + 1), 0x36, 0, bytes([i + 1]) + chunk, len(chunk))
    off += n
send("0x37", 0x37, 0)
send("0x31 F002", 0x31, 0xF002)
send("0x22", 0x22, 0)
PY
