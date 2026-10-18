#!/usr/bin/env bash
# 16-level lab smoke test for the native simulator: opens Settings > General >
# "16-level lab" and checks the presented card numerically (full card, the
# coarse-only reference, a Confirm reload, Back, and the unsupported screen).
#
# Usage: simulator/tests/gray16_lab_smoke.sh <path-to-crosspoint_sim> [work-dir]
set -euo pipefail
SIM="$(realpath "$1")"
WORK="${2:-$(mktemp -d)}"
mkdir -p "$WORK/simfs"
cd "$WORK"
export CROSSPOINT_SIM_FS="$WORK/simfs"
unset CROSSPOINT_SIM_NO_GRAY16

# Buttons are HalGPIO::BTN_* indices: 0 Back, 1 Confirm, 2 Left, 3 Right, 4 Up, 5 Down.
# Home: Up wraps to the last menu item (Settings), Confirm. Settings: General is
# the first section, Confirm. General: Up wraps to the last row (16-level lab), Confirm.
OPEN="--key 4:40:43 --key 1:60:63 --key 1:110:113 --key 4:160:163 --key 1:180:183"
"$SIM" --frames 260 $OPEN --dump lab_full.ppm > lab_full.log 2>&1
"$SIM" --frames 320 $OPEN --key 3:240:243 --dump lab_coarse.ppm > lab_coarse.log 2>&1
"$SIM" --frames 320 $OPEN --key 1:240:243 --dump lab_reload.ppm > lab_reload.log 2>&1
"$SIM" --frames 320 $OPEN --key 0:240:243 --dump lab_back.ppm > lab_back.log 2>&1
CROSSPOINT_SIM_NO_GRAY16=1 "$SIM" --frames 260 $OPEN --dump lab_unsupported.ppm > lab_unsupported.log 2>&1

python3 - <<'PY'
import sys

def ppm(path):
    data = open(path, "rb").read()
    header = data.split(b"\n", 3)
    w, h = map(int, header[1].split())
    return w, h, header[3]

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok = ok and cond

# Portrait: logical x maps to physical row (h - 1 - x), logical y to the physical column.
def lum(img, x, y):
    w, h, px = img
    return px[((h - 1 - x) * w + y) * 3]

def region(img, x0, x1, y0, y1, step=3):
    return {lum(img, x, y) for x in range(x0, x1, step) for y in range(y0, y1, step)}

def all_levels(img):
    return {img[2][i] for i in range(0, len(img[2]), 3)}

W = 480
def band_x(g):
    return g * W // 16, (g + 1) * W // 16

# Bands fill the top 60% of the area above the footer (about 450 px); sample
# well inside each band and below its label box.
def band_levels(img):
    return [region(img, band_x(g)[0] + 4, band_x(g)[1] - 4, 60, 400) for g in range(16)]

# Grid boundaries from the image, scanning x = 330 downwards: band 11 (187)
# ends where grid column 2 starts (levels 2, 6, 10, 14 = 34, 102, 170, 238),
# and the footer below it is white. x = 330 is clear of the column's label
# boxes, which are centred at x = 300.
def grid_bounds(img):
    y = 60
    while lum(img, 330, y) == 187: y += 1
    band_bottom = y
    while lum(img, 330, y) in (34, 102, 170, 238): y += 1
    grid_bottom = y
    return band_bottom, grid_bottom

# 4 x 4 grid: sample a window from each cell's centre downwards (the label box
# sits at the top of the cell).
def cell_levels(img):
    band_bottom, grid_bottom = grid_bounds(img)
    cell_h = (grid_bottom - band_bottom) // 4
    out = []
    for row in range(4):
        cy = band_bottom + (grid_bottom - band_bottom) * (2 * row + 1) // 8
        for col in range(4):
            cx = W * (2 * col + 1) // 8
            out.append(region(img, cx - 30, cx + 30, cy + 4, cy + cell_h // 4, 2))
    return out

ideal = [{g * 17} for g in range(16)]

full = ppm("lab_full.ppm")
check("full card: panel-native 800x480 dump", (full[0], full[1]) == (800, 480))
check("full card: exactly the 16 luminances g*17 appear", all_levels(full) == {g * 17 for g in range(16)})
check("full card: band g is exactly g*17", band_levels(full) == ideal)
check("full card: grid cell (row, col) is exactly ((row<<2)|col)*17", cell_levels(full) == ideal)
log = open("lab_full.log").read()
check("full card: log has the LUT source and pass timings",
      "Fine LUT:" in log and "full card: pass 0" in log and "max block" in log)

coarse = ppm("lab_coarse.ppm")
check("coarse only: band g is 255 - (3 - (g>>2))*68", band_levels(coarse) == [{255 - (3 - (g >> 2)) * 68} for g in range(16)])
check("coarse only: logged as such", "coarse pass only: pass 0" in open("lab_coarse.log").read())

reload = ppm("lab_reload.ppm")
check("confirm: LUT reloaded and card redrawn", band_levels(reload) == ideal and open("lab_reload.log").read().count("Fine LUT:") >= 2)

back = ppm("lab_back.ppm")
check("back: settings screen is pure B/W again", all_levels(back) <= {0, 255})

unsup = ppm("lab_unsupported.ppm")
check("CROSSPOINT_SIM_NO_GRAY16: plain B/W screen", all_levels(unsup) <= {0, 255})
check("CROSSPOINT_SIM_NO_GRAY16: unsupported path logged", "unsupported on this panel" in open("lab_unsupported.log").read())
sys.exit(0 if ok else 1)
PY
