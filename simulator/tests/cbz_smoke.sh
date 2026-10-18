#!/usr/bin/env bash
# Comics smoke test for the native simulator: opens the fixture .cbz from the
# file browser and checks the presented gray levels numerically, in the
# 16-level mode and in the 4-level fallback the device uses today.
#
# Usage: simulator/tests/cbz_smoke.sh <path-to-crosspoint_sim> [work-dir]
set -euo pipefail
SIM="$(realpath "$1")"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${2:-$(mktemp -d)}"
mkdir -p "$WORK/simfs/books"
python3 "$HERE/cbz_fixture.py" "$ROOT" "$WORK/simfs/books/sample.cbz" > /dev/null
cd "$WORK"
export CROSSPOINT_SIM_FS="$WORK/simfs"
unset CROSSPOINT_SIM_NO_GRAY16

# Home -> Browse Files -> books/ -> sample.cbz (button 1 = confirm), then one page forward (button 5).
# Progress persists in the sim filesystem, so it is reset between runs; the plane cache is kept
# on purpose, so the later runs exercise the cached-page path.
OPEN="--key 1:40:43 --key 1:90:93 --key 1:140:143"
"$SIM" --frames 420 $OPEN --dump g16_p1.ppm > g16_p1.log 2>&1
find simfs/.crosspoint -name progress.bin -delete
"$SIM" --frames 560 $OPEN --key 5:420:423 --dump g16_p2.ppm > g16_p2.log 2>&1
find simfs/.crosspoint -name progress.bin -delete
CROSSPOINT_SIM_NO_GRAY16=1 "$SIM" --frames 420 $OPEN --dump g4_p1.ppm > g4_p1.log 2>&1

python3 - <<'PY'
import sys

def ppm(path):
    data = open(path, "rb").read()
    header = data.split(b"\n", 3)
    w, h = map(int, header[1].split())
    return w, h, header[3]

def lum(px, w, x, y):
    return px[(y * w + x) * 3]

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok = ok and cond

# The gradient page has 16 vertical logical bands of 30 px; logical x maps to
# physical row 479 - x in portrait, logical y to the physical column.
def band_means(px, w):
    means = []
    for b in range(16):
        vals = [lum(px, w, X, 479 - x) for x in range(b * 30 + 5, (b + 1) * 30 - 5) for X in range(100, 700, 25)]
        means.append(round(sum(vals) / len(vals)))
    return means

w, h, px = ppm("g16_p1.ppm")
levels = sorted(set(px[i] for i in range(0, len(px), 3)))
check("16-level: exactly the 16 luminances g*17", levels == [g * 17 for g in range(16)])
check("16-level: band means are g*17", band_means(px, w) == [g * 17 for g in range(16)])

w, h, px = ppm("g16_p2.ppm")
top = all(lum(px, w, X, Y) == 255 for X in range(0, 200, 7) for Y in range(0, 480, 13))
bottom = all(lum(px, w, X, Y) == 255 for X in range(600, 760, 7) for Y in range(0, 480, 13))
middle = len(set(lum(px, w, X, Y) for X in range(260, 540, 3) for Y in range(0, 480, 5)))
check("letterboxed page: white above and below, dithered ramp inside (%d levels)" % middle, top and bottom and middle >= 12)

w, h, px = ppm("g4_p1.ppm")
levels = sorted(set(px[i] for i in range(0, len(px), 3)))
expected = [0] * 4 + [85] * 4 + [170] * 4 + [255] * 4
check("4-level fallback: overlay levels and band mapping", levels == [0, 85, 170, 255] and band_means(px, w) == expected)

log = open("g16_p1.log").read()
check("reader loaded 5 pages (junk entries ignored)", "Loaded /books/sample.cbz (5 pages)" in log)
sys.exit(0 if ok else 1)
PY
