#!/usr/bin/env python3
"""
Conversor reproducible de la lamina de sprites -> assets del firmware.

Entrada : assets/source/spritesheet.png  (4 x 4, RGBA, fondo transparente)
Salidas : assets/sprites/hires/<nombre>.png   recorte a resolucion original, con alfa
          assets/sprites/<nombre>.png         sprite a tamano de juego, alfa binaria
          assets/preview/preview_1x.png       hoja a tamano real sobre asfalto/pasto
          assets/preview/preview_4x.png       misma hoja ampliada x4 (vecino mas cercano)
          assets/manifest.json                dimensiones, pivotes, recortes y escalas
          RaceWifi/src/assets/sprites_data.h  RGB565 + mascara 1 bpp en flash
          RaceWifi/src/assets/sprites_data.cpp

Uso:
    tools/.venv/bin/python tools/sprite_convert.py

El proceso es deterministico: misma lamina -> mismos bytes de salida.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "source" / "spritesheet.png"
OUT_SPR = ROOT / "assets" / "sprites"
OUT_HIRES = OUT_SPR / "hires"
OUT_PREV = ROOT / "assets" / "preview"
OUT_MANIFEST = ROOT / "assets" / "manifest.json"
OUT_CODE = ROOT / "RaceWifi" / "src" / "assets"

GRID = 4
BBOX_ALPHA = 16      # alfa minima para considerar un pixel parte del dibujo (incluye particulas)
MASK_ALPHA = 128     # alfa minima para que el pixel quede opaco en el sprite final
CELL_MARGIN = 2      # el bbox no debe tocar el borde de la celda (si no, se estaria cortando)

# Altura objetivo del auto recto en pixeles de pantalla. Todas las variantes de auto
# usan la misma escala, derivada del auto rojo recto.
CAR_TARGET_H = 32
CAR_CANVAS_H = 34
CAR_CANVAS_W_STRAIGHT = 22
CAR_CANVAS_W_TURN = 26

# (fila, columna) -> nombre, grupo
LAYOUT = [
    ["car_red", "car_red_left", "car_red_right", "car_red_brake"],
    ["car_blue", "car_blue_left", "car_blue_right", "car_blue_brake"],
    ["cone", "tires", "tree", "flag"],
    ["smoke_s", "smoke_m", "smoke_l", "spark"],
]

# Escalas fijas por grupo (pixel de pantalla / pixel de la lamina) para no fijarlas
# a mano por sprite: el tamano relativo del dibujo original se conserva dentro del grupo.
GROUP_SCALE = {
    "props": 0.090,   # cono, neumaticos, bandera
    "tree": 0.125,
    "fx": 0.085,      # tres humos (conservan su relacion de tamanos)
    "spark": 0.075,
}
GROUP_OF = {
    "cone": "props", "tires": "props", "flag": "props", "tree": "tree",
    "smoke_s": "fx", "smoke_m": "fx", "smoke_l": "fx", "spark": "spark",
}


def runs(v: np.ndarray) -> list[tuple[int, int]]:
    out, start = [], None
    for i, x in enumerate(v):
        if x > 0 and start is None:
            start = i
        elif x == 0 and start is not None:
            out.append((start, i - 1))
            start = None
    if start is not None:
        out.append((start, len(v) - 1))
    return out


def merge_runs(rs: list[tuple[int, int]], n: int) -> list[tuple[int, int]]:
    """Une corridas separadas por huecos pequenos hasta quedarse con n bandas."""
    rs = list(rs)
    while len(rs) > n:
        gaps = [rs[i + 1][0] - rs[i][1] for i in range(len(rs) - 1)]
        i = int(np.argmin(gaps))
        rs[i:i + 2] = [(rs[i][0], rs[i + 1][1])]
    if len(rs) != n:
        sys.exit(f"Se esperaban {n} bandas y se encontraron {len(rs)}: {rs}")
    return rs


def cell_bounds(bands: list[tuple[int, int]], size: int) -> list[tuple[int, int]]:
    """Limites de celda en el punto medio de cada canaleta vacia."""
    cuts = [0] + [(bands[i][1] + bands[i + 1][0]) // 2 for i in range(len(bands) - 1)] + [size]
    return [(cuts[i], cuts[i + 1]) for i in range(len(bands))]


def lean_of(alpha: np.ndarray) -> float:
    """>0: la trompa (arriba) esta corrida a la derecha respecto de la cola."""
    ys, xs = np.nonzero(alpha >= MASK_ALPHA)
    h = ys.max() - ys.min() + 1
    top = xs[ys < ys.min() + h * 0.25].mean()
    bot = xs[ys > ys.max() - h * 0.25].mean()
    return float(top - bot)


def nn_resample(img: np.ndarray, box: tuple[int, int, int, int], scale: float,
                canvas: tuple[int, int], pivot: tuple[int, int]) -> np.ndarray:
    """Vecino mas cercano: cada pixel de destino toma el pixel de la lamina en el
    centro de su huella. El centro del bbox fuente cae exactamente en el pivote."""
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1 + 1) / 2.0, (y0 + y1 + 1) / 2.0
    cw, ch = canvas
    px, py = pivot
    out = np.zeros((ch, cw, 4), dtype=np.uint8)
    H, W = img.shape[:2]
    for j in range(ch):
        sy = cy + (j + 0.5 - py) / scale
        iy = int(math.floor(sy))
        if not (y0 <= iy <= y1) or iy < 0 or iy >= H:
            continue
        for i in range(cw):
            sx = cx + (i + 0.5 - px) / scale
            ix = int(math.floor(sx))
            if not (x0 <= ix <= x1) or ix < 0 or ix >= W:
                continue
            out[j, i] = img[iy, ix]
    return out


def to565(r: int, g: int, b: int) -> int:
    return (((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)


def from565(v: int) -> tuple[int, int, int]:
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31


def main() -> None:
    sheet = Image.open(SRC).convert("RGBA")
    img = np.array(sheet)
    H, W = img.shape[:2]
    alpha = img[..., 3]

    occ = alpha >= BBOX_ALPHA
    rows = merge_runs(runs(occ.sum(axis=1)), GRID)
    cols = merge_runs(runs(occ.sum(axis=0)), GRID)
    row_cells = cell_bounds(rows, H)
    col_cells = cell_bounds(cols, W)

    # --- recorte de cada celda ---
    crops: dict[str, dict] = {}
    for r in range(GRID):
        for c in range(GRID):
            name = LAYOUT[r][c]
            (cy0, cy1), (cx0, cx1) = row_cells[r], col_cells[c]
            sub = alpha[cy0:cy1, cx0:cx1] >= BBOX_ALPHA
            ys, xs = np.nonzero(sub)
            box = (cx0 + int(xs.min()), cy0 + int(ys.min()), cx0 + int(xs.max()), cy0 + int(ys.max()))
            touches = (box[0] - cx0 < CELL_MARGIN or cx1 - 1 - box[2] < CELL_MARGIN or
                       box[1] - cy0 < CELL_MARGIN or cy1 - 1 - box[3] < CELL_MARGIN)
            if touches:
                sys.exit(f"{name}: el dibujo toca el borde de su celda {(cx0, cy0, cx1, cy1)} -> {box}")
            cell_center = ((cx0 + cx1) / 2, (cy0 + cy1) / 2)
            box_center = ((box[0] + box[2] + 1) / 2, (box[1] + box[3] + 1) / 2)
            crops[name] = {
                "cell": [cx0, cy0, cx1, cy1],
                "bbox": list(box),
                "bbox_size": [box[2] - box[0] + 1, box[3] - box[1] + 1],
                "offset_from_cell_center": [round(box_center[0] - cell_center[0], 1),
                                            round(box_center[1] - cell_center[1], 1)],
            }

    # --- verificacion de la orientacion de los autos ---
    for color in ("red", "blue"):
        for suffix, sign in (("left", -1), ("right", +1)):
            name = f"car_{color}_{suffix}"
            b = crops[name]["bbox"]
            lean = lean_of(alpha[b[1]:b[3] + 1, b[0]:b[2] + 1])
            crops[name]["lean_px"] = round(lean, 1)
            if lean * sign <= 0:
                sys.exit(f"{name}: la inclinacion medida ({lean:.1f}px) no coincide con '{suffix}'")

    # --- escalas y lienzos ---
    car_scale = CAR_TARGET_H / crops["car_red"]["bbox_size"][1]
    specs: dict[str, dict] = {}
    for name in [n for row in LAYOUT for n in row]:
        bw, bh = crops[name]["bbox_size"]
        if name.startswith("car_"):
            scale = car_scale
            turn = name.endswith("_left") or name.endswith("_right")
            cw = CAR_CANVAS_W_TURN if turn else CAR_CANVAS_W_STRAIGHT
            ch = CAR_CANVAS_H
        else:
            scale = GROUP_SCALE[GROUP_OF[name]]
            cw = int(math.ceil(bw * scale)) + 2
            ch = int(math.ceil(bh * scale)) + 2
        pivot = (cw // 2, ch // 2)
        if bw * scale > cw or bh * scale > ch:
            sys.exit(f"{name}: lienzo {cw}x{ch} chico para {bw * scale:.1f}x{bh * scale:.1f}")
        specs[name] = {"scale": scale, "canvas": (cw, ch), "pivot": pivot}

    OUT_SPR.mkdir(parents=True, exist_ok=True)
    OUT_HIRES.mkdir(parents=True, exist_ok=True)
    OUT_PREV.mkdir(parents=True, exist_ok=True)
    OUT_CODE.mkdir(parents=True, exist_ok=True)

    sprites: dict[str, dict] = {}
    for name, sp in specs.items():
        b = crops[name]["bbox"]
        hires = img[b[1]:b[3] + 1, b[0]:b[2] + 1].copy()
        Image.fromarray(hires, "RGBA").save(OUT_HIRES / f"{name}.png", optimize=False)

        small = nn_resample(img, tuple(b), sp["scale"], sp["canvas"], sp["pivot"])
        opaque = small[..., 3] >= MASK_ALPHA
        rgb565 = np.zeros(opaque.shape, dtype=np.uint16)
        for (j, i) in zip(*np.nonzero(opaque)):
            rgb565[j, i] = to565(int(small[j, i, 0]), int(small[j, i, 1]), int(small[j, i, 2]))
        # PNG de juego: exactamente lo que vera la pantalla (colores ya cuantizados a RGB565)
        out = np.zeros_like(small)
        for (j, i) in zip(*np.nonzero(opaque)):
            out[j, i, :3] = from565(int(rgb565[j, i]))
            out[j, i, 3] = 255
        Image.fromarray(out, "RGBA").save(OUT_SPR / f"{name}.png", optimize=False)

        ys, xs = np.nonzero(opaque)
        sprites[name] = {
            "w": sp["canvas"][0], "h": sp["canvas"][1],
            "pivot": list(sp["pivot"]),
            "scale": round(sp["scale"], 5),
            "opaque_bbox": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())],
            "opaque_px": int(opaque.sum()),
            "rgb565": rgb565, "mask": opaque, "rgba": out,
        }
        # el contenido no puede tocar el borde del lienzo (seria un recorte)
        ob = sprites[name]["opaque_bbox"]
        if ob[0] == 0 or ob[1] == 0 or ob[2] == sp["canvas"][0] - 1 or ob[3] == sp["canvas"][1] - 1:
            sys.exit(f"{name}: el sprite toca el borde del lienzo {sp['canvas']} -> {ob}")

    write_previews(sprites)
    write_manifest(crops, sprites, car_scale)
    write_code(sprites)
    print(f"OK: {len(sprites)} sprites; escala autos {car_scale:.4f}")
    for n, s in sprites.items():
        print(f"  {n:16s} {s['w']:2d}x{s['h']:2d} pivot={tuple(s['pivot'])} opacos={s['opaque_px']}")


def write_previews(sprites: dict[str, dict]) -> None:
    names = [n for row in LAYOUT for n in row]
    cell_w = max(s["w"] for s in sprites.values()) + 6
    cell_h = max(s["h"] for s in sprites.values()) + 6
    # dos fondos por sprite: asfalto y pasto (los colores del juego)
    asphalt, grass = (0x52, 0x55, 0x5A), (0x2E, 0x8B, 0x3A)
    sheet = Image.new("RGBA", (GRID * cell_w * 2, GRID * cell_h), (0, 0, 0, 255))
    for k, n in enumerate(names):
        r, c = divmod(k, GRID)
        for t, bg in enumerate((asphalt, grass)):
            x = (c * 2 + t) * cell_w
            y = r * cell_h
            tile = Image.new("RGBA", (cell_w, cell_h), bg + (255,))
            spr = Image.fromarray(sprites[n]["rgba"], "RGBA")
            tile.alpha_composite(spr, ((cell_w - sprites[n]["w"]) // 2, (cell_h - sprites[n]["h"]) // 2))
            sheet.paste(tile, (x, y))
    sheet.convert("RGB").save(OUT_PREV / "preview_1x.png")
    sheet.convert("RGB").resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(OUT_PREV / "preview_4x.png")


def write_manifest(crops: dict, sprites: dict, car_scale: float) -> None:
    man = {
        "source": str(SRC.relative_to(ROOT)),
        "format": "RGB565 little-endian (uint16 nativo) + mascara 1 bpp, MSB = pixel izquierdo, filas alineadas a byte",
        "bbox_alpha_threshold": BBOX_ALPHA,
        "mask_alpha_threshold": MASK_ALPHA,
        "resample": "vecino mas cercano, muestreo en el centro de la huella de cada pixel destino",
        "car_scale": round(car_scale, 5),
        "pivot_note": "El pivote es el punto del sprite que se ubica en la posicion logica del objeto (centro del auto).",
        "sprites": {},
    }
    for n, s in sprites.items():
        man["sprites"][n] = {
            "w": s["w"], "h": s["h"], "pivot": s["pivot"], "scale": s["scale"],
            "opaque_bbox": s["opaque_bbox"], "opaque_px": s["opaque_px"],
            "source": crops[n],
        }
    OUT_MANIFEST.write_text(json.dumps(man, indent=2, ensure_ascii=False) + "\n")


def write_code(sprites: dict[str, dict]) -> None:
    names = [n for row in LAYOUT for n in row]
    enum = "\n".join(f"    SPR_{n.upper()} = {i}," for i, n in enumerate(names))
    hdr = f"""// GENERADO por tools/sprite_convert.py - no editar a mano.
#pragma once
#include <stdint.h>

// Pixeles RGB565 en el orden nativo de uint16 (el panel se configura little-endian).
// Mascara: 1 bit por pixel, MSB = pixel de la izquierda, cada fila alineada a byte.
struct Sprite {{
    uint8_t w, h;
    int8_t pivotX, pivotY;
    const uint16_t* pixels;
    const uint8_t* mask;
}};

enum SpriteId : uint8_t {{
{enum}
    SPR_COUNT = {len(names)}
}};

extern const Sprite kSprites[SPR_COUNT];
"""
    parts = ["// GENERADO por tools/sprite_convert.py - no editar a mano.",
             '#include "sprites_data.h"', ""]
    for n in names:
        s = sprites[n]
        w, h = s["w"], s["h"]
        px = s["rgb565"].ravel()
        lines = []
        for j in range(h):
            row = px[j * w:(j + 1) * w]
            lines.append("    " + ",".join(f"0x{int(v):04X}" for v in row) + ",")
        parts.append(f"static const uint16_t px_{n}[{w * h}] = {{")
        parts.extend(lines)
        parts.append("};")
        stride = (w + 7) // 8
        mbytes = []
        for j in range(h):
            for k in range(stride):
                byte = 0
                for bit in range(8):
                    i = k * 8 + bit
                    if i < w and s["mask"][j, i]:
                        byte |= 0x80 >> bit
                mbytes.append(byte)
        parts.append(f"static const uint8_t mk_{n}[{len(mbytes)}] = {{")
        for j in range(h):
            parts.append("    " + ",".join(f"0x{b:02X}" for b in mbytes[j * stride:(j + 1) * stride]) + ",")
        parts.append("};")
        parts.append("")
    parts.append("const Sprite kSprites[SPR_COUNT] = {")
    for n in names:
        s = sprites[n]
        parts.append(f"    {{{s['w']}, {s['h']}, {s['pivot'][0]}, {s['pivot'][1]}, px_{n}, mk_{n}}},")
    parts.append("};")
    (OUT_CODE / "sprites_data.h").write_text(hdr)
    (OUT_CODE / "sprites_data.cpp").write_text("\n".join(parts) + "\n")


if __name__ == "__main__":
    main()
