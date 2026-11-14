"""Build the vila-glt icon set from the two SVG sources in media/.

Sources (Material Symbols "interpreter_mode", Rounded, fill, weight 700):
  media/icon.svg        optical size 48, used for 32 px and up
  media/icon-small.svg  optical size 20, used for 16 and 24 px

Outputs (in media/):
  icon.png        512 px, blue (#1A73E8) for light backgrounds
  icon-light.png  512 px, light grey (#E3E3E3) for dark backgrounds
  icon.ico        blue, 16, 24, 32, 48, 64, 128, 256 px (installer, Windows Apps)
  icon.svg / icon-small.svg are rewritten blue, with a square viewBox fitted
  to the artwork (Material paths overshoot the 0..960 box and would be clipped).

To start from a fresh download, pass the downloaded SVGs instead of the
media/ ones; any fill colour works, it is replaced.

Requires: pip install pillow resvg-py   (use a venv, not the system Python)

usage:
  python tools/build-icons.py                     build from media/icon*.svg
  python tools/build-icons.py build  <opsz48.svg> <opsz20.svg> [media-dir]
  python tools/build-icons.py preview <opsz48.svg> <opsz20.svg> <out.png>
"""
import io
import re
import sys

import resvg_py
from PIL import Image, ImageDraw

BLUE, LIGHT = "#1A73E8", "#E3E3E3"
OBS_DARK, WHITE = "#1E1F22", "#FFFFFF"
SMALL_MAX = 24  # sizes up to this use the optical-size-20 drawing
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def path_of(svg_file):
    text = open(svg_file, encoding="utf-8").read()
    return re.search(r'<path[^>]*\sd="([^"]+)"', text).group(1)


def svg_text(d, view_box, fill, size=None):
    wh = f'width="{size}" height="{size}" ' if size else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" {wh}viewBox="{view_box}">'
            f'<path fill="{fill}" d="{d}"/></svg>')


def render(text):
    data = bytes(resvg_py.svg_to_bytes(svg_string=text))
    return Image.open(io.BytesIO(data)).convert("RGBA")


def fitted_view_box(d):
    """Square viewBox around the real artwork, with a ~2 % margin per side."""
    px, py, pw, ph = -100, -1000, 1200, 1100
    probe = render(f'<svg xmlns="http://www.w3.org/2000/svg" width="{pw}" height="{ph}" '
                   f'viewBox="{px} {py} {pw} {ph}"><path d="{d}"/></svg>')
    l, t, r, b = probe.getchannel("A").getbbox()
    x0, y0, x1, y1 = px + l, py + t, px + r, py + b
    side = max(x1 - x0, y1 - y0) * 1.04
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    return f"{cx - side / 2:.2f} {cy - side / 2:.2f} {side:.2f} {side:.2f}"


def main():
    args = sys.argv[1:] or ["build", "media/icon.svg", "media/icon-small.svg", "media"]
    mode, f48, f20 = args[0], args[1], args[2]
    out = args[3] if len(args) > 3 else "media"
    d48, d20 = path_of(f48), path_of(f20)
    vb48, vb20 = fitted_view_box(d48), fitted_view_box(d20)

    def icon(size, fill, small=None):
        small = size <= SMALL_MAX if small is None else small
        d, vb = (d20, vb20) if small else (d48, vb48)
        return render(svg_text(d, vb, fill, size))

    if mode == "preview":
        cols = [(16, True), (24, True), (32, True), (32, False), (48, False), (256, False)]
        rows = [(BLUE, WHITE), (LIGHT, OBS_DARK)]
        pad, zoom = 16, 6
        width = pad + sum(s + pad for s, _ in cols) + 2 * (16 * zoom + pad)
        row_h = 256 + 2 * pad
        sheet = Image.new("RGBA", (width, row_h * len(rows)), WHITE)
        for ri, (fg, bg) in enumerate(rows):
            y = ri * row_h
            ImageDraw.Draw(sheet).rectangle([0, y, width, y + row_h - 1], fill=bg)
            x = pad
            for s, small in cols:
                sheet.alpha_composite(icon(s, fg, small), (x, y + pad + (256 - s) // 2))
                x += s + pad
            for small in (True, False):  # 16 px zoomed: optical size 20 vs 48
                big = icon(16, fg, small).resize((16 * zoom, 16 * zoom), Image.NEAREST)
                sheet.alpha_composite(big, (x, y + pad + (256 - 16 * zoom) // 2))
                x += 16 * zoom + pad
        sheet.convert("RGB").save(out)
        print("preview:", out)
        return

    for name, d, vb in (("icon.svg", d48, vb48), ("icon-small.svg", d20, vb20)):
        with open(f"{out}/{name}", "w", encoding="utf-8", newline="\n") as f:
            f.write(svg_text(d, vb, BLUE) + "\n")
    icon(512, BLUE).save(f"{out}/icon.png", optimize=True)
    icon(512, LIGHT).save(f"{out}/icon-light.png", optimize=True)
    frames = [icon(s, BLUE) for s in ICO_SIZES]
    frames[-1].save(f"{out}/icon.ico", format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])
    print(f"icons written to {out}/")


main()
