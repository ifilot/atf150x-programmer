#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors

## @file
# Draws the application icon (a PLCC-44 package whose die shows a small fuse
# grid in the fuse-map palette) and the Program and Install Firmware toolbar
# icons. Requires Pillow. Run from the repository root:
#
#     python3 gui/tools/generate_icon.py

import pathlib

from PIL import Image, ImageDraw

OUTPUT = pathlib.Path(__file__).resolve().parent.parent / "resources" / "icons"
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
BODY = (46, 52, 64, 255)
BODY_EDGE = (28, 32, 40, 255)
PIN = (196, 200, 208, 255)
PIN_EDGE = (120, 126, 136, 255)
# Programmed-fuse colors shared with FuseMapWidget's region palette.
PALETTE = [
    (37, 99, 235, 255),   # product terms
    (234, 88, 12, 255),   # macrocell configuration
    (22, 163, 74, 255),   # interconnect
    (147, 51, 234, 255),  # global OE
    (220, 38, 38, 255),   # configuration
    (202, 138, 4, 255),   # user signature
]
ERASED = (236, 240, 246, 255)
PATTERN = [
    "0x1x",
    "x2x0",
    "3x04",
    "x5x0",
]


## Renders one square icon at the requested pixel size.
# @param size Edge length in pixels.
# @return RGBA image.
def render(size):
    scale = 8 if size < 64 else 4
    canvas = size * scale
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    unit = canvas / 64.0

    def box(x0, y0, x1, y1):
        return [round(x0 * unit), round(y0 * unit),
                round(x1 * unit), round(y1 * unit)]

    # Small renderings keep fewer, larger leads so they stay legible.
    pins = 11 if size >= 48 else (7 if size >= 24 else 5)
    lead_span = 40.0
    pitch = lead_span / pins
    lead_width = pitch * 0.55
    for i in range(pins):
        center = 12 + pitch * (i + 0.5)
        half = lead_width / 2
        for rect in (
            box(center - half, 2, center + half, 10),
            box(center - half, 54, center + half, 62),
            box(2, center - half, 10, center + half),
            box(54, center - half, 62, center + half),
        ):
            draw.rectangle(rect, fill=PIN, outline=PIN_EDGE,
                           width=max(1, round(0.6 * unit)))

    # The chamfered body corner marks pin 1, as on a real PLCC package.
    body = [(8, 13), (13, 8), (56, 8), (56, 56), (8, 56)]
    draw.polygon([(x * unit, y * unit) for x, y in body], fill=BODY,
                 outline=BODY_EDGE, width=max(1, round(1.2 * unit)))

    grid_origin = 17.0
    cell = 30.0 / len(PATTERN)
    gap = 1.6 if size >= 32 else 2.4
    for row, line in enumerate(PATTERN):
        for col, symbol in enumerate(line):
            color = ERASED if symbol == "x" else PALETTE[int(symbol)]
            x0 = grid_origin + col * cell + gap / 2
            y0 = grid_origin + row * cell + gap / 2
            draw.rounded_rectangle(
                box(x0, y0, x0 + cell - gap, y0 + cell - gap),
                radius=round(0.8 * unit), fill=color)
    return image.resize((size, size), Image.Resampling.LANCZOS)


## Draws a bold arrow on a 64-unit canvas.
# @param draw Pillow drawing context.
# @param unit Pixels per canvas unit.
# @param color Fill color.
# @param up True for an upward arrow.
def arrow(draw, unit, color, up):
    shaft = [(26, 6), (38, 6), (38, 26), (48, 26), (32, 44), (16, 26),
             (26, 26)]
    if up:
        shaft = [(x, 50 - y) for x, y in shaft]
    points = [(x * unit, y * unit) for x, y in shaft]
    draw.polygon(points, fill=color, outline=(255, 255, 255, 255),
                 width=max(1, round(2.5 * unit)))


## Renders a toolbar icon: a chip receiving a design, or a board receiving
# firmware.
# @param size Edge length in pixels.
# @param kind "program" or "firmware".
# @return RGBA image.
def render_tool(size, kind):
    scale = 8
    canvas = size * scale
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    unit = canvas / 64.0

    def box(x0, y0, x1, y1):
        return [round(x0 * unit), round(y0 * unit),
                round(x1 * unit), round(y1 * unit)]

    if kind == "program":
        for i in range(4):
            y = 30 + i * 8
            draw.rectangle(box(6, y, 14, y + 4), fill=PIN, outline=PIN_EDGE)
            draw.rectangle(box(50, y, 58, y + 4), fill=PIN, outline=PIN_EDGE)
        draw.rounded_rectangle(box(12, 24, 52, 62), radius=round(3 * unit),
                               fill=BODY, outline=BODY_EDGE,
                               width=max(1, round(1.5 * unit)))
        for row in range(2):
            for col in range(3):
                color = PALETTE[(row * 3 + col) % len(PALETTE)]
                x0 = 19 + col * 9.5
                y0 = 40 + row * 9.5
                draw.rectangle(box(x0, y0, x0 + 7, y0 + 7), fill=color)
        arrow(draw, unit, (37, 99, 235, 255), up=False)
    else:
        board = (0, 151, 157, 255)
        draw.rounded_rectangle(box(4, 26, 60, 62), radius=round(4 * unit),
                               fill=board, outline=(0, 102, 107, 255),
                               width=max(1, round(1.5 * unit)))
        draw.rectangle(box(22, 38, 42, 56), fill=BODY)
        for i in range(5):
            x = 10 + i * 10
            draw.rectangle(box(x, 28, x + 5, 32), fill=(255, 214, 102, 255))
        arrow(draw, unit, (22, 163, 74, 255), up=True)
    return image.resize((size, size), Image.Resampling.LANCZOS)


## Writes the multi-resolution ICO, a 256-pixel PNG and toolbar icons.
def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    images = [render(size) for size in SIZES]
    images[-1].save(OUTPUT / "atf150x-programmer.png")
    images[-1].save(OUTPUT / "atf150x-programmer.ico",
                    sizes=[(size, size) for size in SIZES],
                    append_images=images[:-1])
    for kind in ("program", "firmware"):
        render_tool(24, kind).save(OUTPUT / f"{kind}.png")
        render_tool(48, kind).save(OUTPUT / f"{kind}@2x.png")
    print(f"Wrote icons to {OUTPUT}")


if __name__ == "__main__":
    main()
