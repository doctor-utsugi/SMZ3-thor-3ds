#!/usr/bin/env bash
# Builds the PC frontend (build/pc/smz3): the shared C core, snes9x (libretro core, statically linked) and SDL2.
# Needs a C and C++ compiler, make, python3 with Pillow only for tools/gen_pc_font.py (the generated font is committed) and the SDL2 development files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
S9X="$ROOT/vendor/snes9x_libretro"
S9X_COMMIT=fae2fea08f74180759ef540ee94259213f503480
if [ ! -d "$S9X/.git" ]; then
    mkdir -p "$ROOT/vendor"
    git clone https://github.com/libretro/snes9x.git "$S9X"
fi
git -C "$S9X" checkout -q "$S9X_COMMIT"
[ -d "$ROOT/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/ips" ] || { echo "the IPS patches are expected in vendor/Lemuroid (see build-android.sh)" >&2; exit 1; }
make -C "$ROOT/frontends/pc" -j"$(nproc)"
echo "built $ROOT/build/pc/smz3"
