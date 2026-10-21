#!/bin/bash
# OP-TEE console. The Linux window is run_SecOC.sh or run_OTA.sh, one at a time.
set -euo pipefail

PORT=54321

echo "This window is the OP-TEE console."
echo "The Linux window is scripts/aarch32/run_SecOC.sh or scripts/aarch32/run_OTA.sh."
echo "Leave this window with Ctrl-]."

exec python3 - "$PORT" <<'PY'
import os
import select
import socket
import sys
import termios
import time
import tty

port = int(sys.argv[1])
deadline = time.time() + 60
sock = None
while time.time() < deadline:
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        break
    except OSError:
        time.sleep(0.2)
if sock is None:
    sys.stderr.write(
        "The Linux console is not listening on 127.0.0.1:%d.\n"
        "Start scripts/aarch32/run_SecOC.sh or scripts/aarch32/run_OTA.sh "
        "in the other terminal.\n" % port
    )
    sys.exit(1)

sock.setblocking(False)
stdin = sys.stdin.fileno()
stdout = sys.stdout.fileno()
tty_fd = None
old = None
if os.isatty(stdin):
    tty_fd = stdin
    old = termios.tcgetattr(tty_fd)
    tty.setraw(tty_fd)
try:
    while True:
        watch = [sock]
        if tty_fd is not None:
            watch.append(stdin)
        readable, _, _ = select.select(watch, [], [])
        if tty_fd is not None and stdin in readable:
            data = os.read(stdin, 1024)
            if not data or data == b"\x1d":
                break
            sock.sendall(data)
        if sock in readable:
            data = sock.recv(4096)
            if not data:
                break
            os.write(stdout, data)
finally:
    if old is not None:
        termios.tcsetattr(tty_fd, termios.TCSADRAIN, old)
    sock.close()
PY
