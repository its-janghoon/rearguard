#!/usr/bin/env python3
"""Run one shell command on the QNX guest through the QEMU serial console socket.

usage: con.py 'command' [timeout_s]
Prints the command output (between unique markers). Exit code 1 on timeout.
"""
import os, socket, sys, time, uuid

SOCK = os.path.expanduser("~/qnx-setup/run/serial.sock")
cmd = sys.argv[1]
timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 30
tag = uuid.uuid4().hex[:8]
s = socket.socket(socket.AF_UNIX)
s.connect(SOCK)
s.settimeout(0.5)
s.sendall(b"\r")
time.sleep(0.3)
s.sendall(f"echo B{tag}; {cmd}; echo E{tag}=$?\r".encode())
buf, end = b"", time.time() + timeout
while time.time() < end:
    try:
        d = s.recv(65536)
        if d:
            buf += d
    except socket.timeout:
        pass
    t = buf.decode("utf-8", "replace").replace("\r", "")
    import re
    m = re.search(rf"B{tag}\n(.*?)E{tag}=(\d+)", t, re.S)
    if m:
        sys.stdout.write(m.group(1))
        sys.exit(int(m.group(2)) and 2)
sys.stdout.write(buf.decode("utf-8", "replace")[-3000:])
sys.exit(1)
