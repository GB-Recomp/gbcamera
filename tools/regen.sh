#!/usr/bin/env bash
#
# Regenerate the recompiled C sources in this repo from a user-supplied
# Game Boy Camera ROM via gb-recompiled. No pret/decompilation exists for
# the Camera so we recompile straight from the ROM — symbol names will be
# generic (func_BB_HHHH) rather than human-readable.
#
# Usage:
#   tools/regen.sh <path-to-gbcamera-rom.gb> <path-to-gb-recompiled>
#
# Requirements:
#   - cmake, a C/C++ compiler — to build gbrecomp
#   - python3                  — for the bss_rom_data step (lives in
#                                 GB-Recomp/pgbcomp/tools/ today)
#
# What it does:
#   1. Build gbrecomp from the gb-recompiled clone.
#   2. Run gbrecomp with --emit-asset-loader --prefix-symbols on the ROM,
#      symlinked under the name `gbcamera.gb` so the prefix comes out clean.
#   3. BSS-ify the rom_data buffer.
#   4. Copy the generated sources into this repo (overwriting the prior
#      regen). The minimal assets_manifest_gbcamera.h stays — it isn't
#      regenerated since there's no .map file to drive it.

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <gbcamera-rom.gb> <gb-recompiled-path>" >&2
    exit 1
fi

ROM_PATH="$(realpath "$1")"
GBRECOMP_DIR="$(realpath "$2")"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

[[ -f "$ROM_PATH" ]]      || { echo "ROM missing: $ROM_PATH" >&2; exit 1; }
[[ -d "$GBRECOMP_DIR" ]]  || { echo "gb-recompiled dir missing: $GBRECOMP_DIR" >&2; exit 1; }

echo "[regen] ROM   : $ROM_PATH"
echo "[regen] gbrec : $GBRECOMP_DIR"
echo "[regen] repo  : $REPO_DIR"

# 1. Build gbrecomp.
echo "[regen] Building gbrecomp..."
(cd "$GBRECOMP_DIR" && cmake -B build -S . > /dev/null && cmake --build build -j"$(nproc)" --target gbrecomp)
GBRECOMP_BIN="$GBRECOMP_DIR/build/bin/gbrecomp"
[[ -x "$GBRECOMP_BIN" ]] || GBRECOMP_BIN="$GBRECOMP_DIR/build/recompiler/gbrecomp"
[[ -x "$GBRECOMP_BIN" ]] || { echo "Missing gbrecomp binary" >&2; exit 1; }

# 2. Recompile under a clean prefix.
WORK="$(mktemp -d)"
trap "rm -rf '$WORK'" EXIT
ln -sf "$ROM_PATH" "$WORK/gbcamera.gb"
echo "[regen] Running gbrecomp..."
"$GBRECOMP_BIN" --emit-asset-loader --prefix-symbols -o "$WORK/output" "$WORK/gbcamera.gb"

# 3. BSS-ify rom_data.
BSS_TOOL="$GBRECOMP_DIR/tools/bss_rom_data.py"
[[ -f "$BSS_TOOL" ]] || BSS_TOOL="$(dirname "$REPO_DIR")/pgbcomp/tools/bss_rom_data.py"
if [[ -f "$BSS_TOOL" ]]; then
    echo "[regen] BSS-ifying rom_data..."
    python3 "$BSS_TOOL" "$WORK/output/gbcamera_rom.c" "$WORK/output/gbcamera.c"
else
    echo "[regen] WARNING: bss_rom_data.py not found; rom_data will stay in .data"
fi

# 4. Copy back. Keep CMakeLists.txt, launcher_main.cpp, README, .gitignore,
#    tools/, and the minimal assets_manifest_gbcamera.h — those are repo-
#    level, not regenerated.
echo "[regen] Copying generated sources into repo..."
cp -v "$WORK/output/gbcamera_main.c" "$REPO_DIR/"
cp -v "$WORK/output/gbcamera.c" "$REPO_DIR/"
cp -v "$WORK/output/gbcamera_rom.c" "$REPO_DIR/"
cp -v "$WORK/output/gbcamera_dispatch_chunk_"*.c "$REPO_DIR/"
cp -v "$WORK/output/gbcamera_funcs_"*.c "$REPO_DIR/"
cp -v "$WORK/output/gbcamera.h" "$REPO_DIR/"
cp -v "$WORK/output/gbcamera_internal.h" "$REPO_DIR/"
cp -v "$WORK/output/gbcamera_metadata.json" "$REPO_DIR/" 2>/dev/null || true

echo "[regen] Done."
echo "[regen] If the funcs file list changed, update CMakeLists.txt's GBCAMERA_SOURCES."
echo "[regen] Build & test:"
echo "  (cd $REPO_DIR && cmake -B build -S . && cmake --build build -j\$(nproc))"
