#!/usr/bin/env bash
# Bake a HotSync-ready POSE64 baseline session that contains an (empty)
# DatebookDB, for the device-e2e harness (WILDPALMS_PALM_BASELINE_PSF).
#
# Usage: make-baseline.sh <pose64-bin> <fresh-input.psf> <output-baseline.psf>
# Example:
#   make-baseline.sh /home/clinton/dev/POSE64/build/pose64 \
#       /home/clinton/dev/POSE64/freshm515.psf \
#       /home/clinton/dev/POSE64/baseline-datebook-m515.psf
set -euo pipefail

BIN="${1:?pose64 binary path}"
FRESH="${2:?fresh input .psf}"
OUT="${3:?output baseline .psf}"
PORT="${PORT:-6499}"
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "Compiling mkdatebook..."
gcc "$HERE/mkdatebook.c" -o "$HERE/mkdatebook" -I/usr/include -lpisock
PDB="$(mktemp --suffix=.pdb)"
"$HERE/mkdatebook" "$PDB"

echo "Baking baseline via POSE64 ReControl on port $PORT..."
python3 - "$BIN" "$FRESH" "$OUT" "$PDB" "$PORT" <<'PY'
import socket, subprocess, time, os, sys
BIN, FRESH, OUT, PDB, PORT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
def send(cmd, ms=2000):
    s = socket.create_connection(("localhost", PORT), timeout=8); s.settimeout(0.3)
    s.sendall((cmd + "\n").encode("latin-1")); buf = b""; end = time.time() + ms/1000.0
    while time.time() < end:
        try:
            c = s.recv(4096)
            if not c: break
            buf += c
        except socket.timeout:
            if buf: break
    s.close(); return buf.decode("latin-1", "replace").strip()
env = dict(os.environ); env["QT_QPA_PLATFORM"] = "offscreen"
proc = subprocess.Popen([BIN, "-psf", FRESH, "--port", str(PORT)], env=env,
                        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
try:
    ok = False
    for _ in range(60):
        try:
            if send("state", 300).startswith("OK"): ok = True; break
        except OSError: pass
        time.sleep(0.4)
    assert ok, "ReControl never became ready"
    assert send(f"install {PDB}", 8000).startswith("OK"), "install failed"
    time.sleep(0.5)
    assert "DatebookDB type=DATA creator=date" in send("apps all", 2500), "DatebookDB not present after install"
    assert send(f"save {OUT}", 8000).startswith("OK"), "save failed"
    send("quit", 1000)
finally:
    try: proc.wait(timeout=5)
    except Exception: proc.kill()
assert os.path.exists(OUT), "baseline not written"
print(f"baseline written: {OUT} ({os.path.getsize(OUT)} bytes)")
PY
rm -f "$PDB"
echo "Done. Point WILDPALMS_PALM_BASELINE_PSF at $OUT"
