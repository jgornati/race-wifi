#!/usr/bin/env bash
# Compila la logica portable del firmware para la PC y corre las pruebas.
# Las capturas de pantalla quedan en build/host_out/*.png
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/host_out"
mkdir -p "$OUT"
rm -f "$OUT"/*.ppm "$OUT"/*.png

SRC="$ROOT/RaceWifi/src"
CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -o "$ROOT/build/host_tests" \
    "$ROOT/tools/host/host_tests.cpp" "$ROOT/tools/host/fake_platform.cpp" \
    "$SRC/app/app.cpp" "$SRC/app/app_draw.cpp" "$SRC/app/app_diag.cpp" \
    "$SRC/display/canvas.cpp" "$SRC/assets/sprites_data.cpp" \
    "$SRC/game/track.cpp" "$SRC/game/race.cpp" "$SRC/game/fx.cpp" \
    "$SRC/input/button.cpp" "$SRC/input/tilt.cpp" \
    "$SRC/net/protocol.cpp" "$SRC/net/link.cpp" "$SRC/net/smoothing.cpp"

status=0
"$ROOT/build/host_tests" "$OUT" || status=$?

# PPM -> PNG (x2) para poder mirarlas
if [ -x "$ROOT/tools/.venv/bin/python" ]; then
    "$ROOT/tools/.venv/bin/python" - "$OUT" <<'EOF'
import sys, pathlib
from PIL import Image
out = pathlib.Path(sys.argv[1])
for p in sorted(out.glob("*.ppm")):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
EOF
fi
exit $status
