#!/usr/bin/env python3
"""Pack the title logo like vanilla FireRed: Pokémon in the left 176px,
version text in the right 80px. Restores the vanilla tilemap so the version
sits under the logo instead of the screen's right edge."""
from pathlib import Path
import struct
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DST = ROOT / "graphics/title_screen/firered/game_title_logo.png"
DST_PAL = ROOT / "graphics/title_screen/firered/game_title_logo.pal"
DST_BIN = ROOT / "graphics/title_screen/firered/game_title_logo.bin"
CHROMA = (0, 255, 41)
W, H = 256, 64
MAX_COLORS = 192
POKE_W = 176  # 22 tiles, same as vanilla row 0
VER_X, VER_W, VER_H = 176, 80, 56  # PNG rows 0-6; map rows 7-13


def is_chroma(r, g, b):
    if g >= 90 and g >= r + 25 and g >= b + 25:
        return True
    if g >= 140 and r <= 120 and b <= 120:
        return True
    return False


def gba(c):
    return (c >> 3) << 3


def snap(r, g, b, crush_dark=False):
    if is_chroma(r, g, b):
        return CHROMA
    if crush_dark and r + g + b <= 60:
        return (0, 0, 0)
    return (gba(r), gba(g), gba(b))


def content_bbox(im):
    px = im.load()
    w, h = im.size
    minx, miny, maxx, maxy = w, h, 0, 0
    found = False
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y][:3]
            if not is_chroma(r, g, b):
                found = True
                minx, maxx = min(minx, x), max(maxx, x)
                miny, maxy = min(miny, y), max(maxy, y)
    if not found:
        return (0, 0, w, h)
    return (minx, miny, maxx + 1, maxy + 1)


def split_groups(im):
    """Split non-chroma into left (Pokémon) and right (version) by chroma gap."""
    px = im.load()
    w, h = im.size
    cols = []
    for x in range(w):
        hit = False
        for y in range(h):
            r, g, b = px[x, y][:3]
            if not is_chroma(r, g, b):
                hit = True
                break
        cols.append(hit)
    groups = []
    i = 0
    while i < w:
        if not cols[i]:
            i += 1
            continue
        j = i
        while j < w and cols[j]:
            j += 1
        groups.append((i, j))
        i = j
    if len(groups) >= 2:
        return groups[0], groups[-1]
    if groups:
        a, b = groups[0]
        mid = (a + b) // 2
        return (a, mid), (mid, b)
    return (0, w // 2), (w // 2, w)


def fit_into(src, box_w, box_h):
    bb = content_bbox(src)
    crop = src.crop(bb)
    cw, ch = crop.size
    scale = min(box_w / max(cw, 1), box_h / max(ch, 1))
    nw, nh = max(1, int(cw * scale)), max(1, int(ch * scale))
    return crop.resize((nw, nh), Image.Resampling.NEAREST)


def split_horizontal(im):
    px = im.load()
    w, h = im.size
    rows = []
    for y in range(h):
        hit = False
        for x in range(w):
            r, g, b = px[x, y][:3]
            if not is_chroma(r, g, b):
                hit = True
                break
        rows.append(hit)
    groups = []
    i = 0
    while i < h:
        if not rows[i]:
            i += 1
            continue
        j = i
        while j < h and rows[j]:
            j += 1
        groups.append((i, j))
        i = j
    if len(groups) >= 2:
        return groups[0], groups[-1]
    if groups:
        a, b = groups[0]
        mid = a + int((b - a) * 0.62)
        return (a, mid), (mid, b)
    return (0, h // 2), (h // 2, h)


def restore_vanilla_bin():
    data = subprocess.check_output(
        ["git", "show", "HEAD:graphics/title_screen/firered/game_title_logo.bin"],
        cwd=ROOT,
    )
    entries = list(struct.unpack("<%dH" % (len(data) // 2), data))
    # Show PNG rows 5-6 of the version strip (tiles 182-191, 214-223).
    for i, tile in enumerate(range(182, 192)):
        entries[12 * 32 + 6 + i] = tile
    for i, tile in enumerate(range(214, 224)):
        entries[13 * 32 + 6 + i] = tile
    DST_BIN.write_bytes(struct.pack("<%dH" % len(entries), *entries))


def write_indexed(canvas, crush_dark=False):
    px = canvas.load()
    colors = {CHROMA: 0}
    ordered = [CHROMA]
    for y in range(H):
        for x in range(W):
            col = snap(*px[x, y], crush_dark=crush_dark)
            px[x, y] = col
            if col in colors:
                continue
            if len(ordered) < MAX_COLORS:
                colors[col] = len(ordered)
                ordered.append(col)
            else:
                best, bd = 1, 1 << 30
                r, g, b = col
                for i, (cr, cg, cb) in enumerate(ordered[1:], 1):
                    d = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
                    if d < bd:
                        bd, best = d, i
                colors[col] = best

    pal = []
    for rgb in ordered:
        pal.extend(rgb)
    pal.extend([0, 0, 0] * (256 - len(ordered)))

    indexed = Image.new("P", (W, H))
    indexed.putpalette(pal)
    indexed.putdata([colors[px[x, y]] for y in range(H) for x in range(W)])
    indexed.save(DST, optimize=False)
    with open(DST_PAL, "w", newline="\n") as f:
        f.write("JASC-PAL\n0100\n256\n")
        for i in range(256):
            f.write("%d %d %d\n" % (pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]))
    restore_vanilla_bin()
    print("wrote", DST.name, "colors", len(ordered), "bin", DST_BIN.stat().st_size)


def pack_keep_layout(src_path):
    """GBA 8bpp + chroma only. Same pixels, no crop/scale/split."""
    src = Image.open(src_path).convert("RGB")
    canvas = Image.new("RGB", (W, H), CHROMA)
    canvas.paste(src.crop((0, 0, min(src.size[0], W), min(src.size[1], H))), (0, 0))
    write_indexed(canvas, crush_dark=False)


def pack_relayout():
    src = Image.open(DST).convert("RGB")
    poke_range, ver_range = split_groups(src)
    poke = src.crop((poke_range[0], 0, poke_range[1], src.size[1]))
    ver = src.crop((ver_range[0], 0, ver_range[1], src.size[1]))

    canvas = Image.new("RGB", (W, H), CHROMA)
    poke_fit = fit_into(poke, POKE_W - 4, H - 4)
    title_box, ver_box = split_horizontal(ver)
    title_im = ver.crop((0, title_box[0], ver.size[0], title_box[1]))
    ver_im = ver.crop((0, ver_box[0], ver.size[0], ver_box[1]))
    title_fit = fit_into(title_im, VER_W - 4, 34)
    ver_fit = fit_into(ver_im, VER_W - 8, 18)
    canvas.paste(poke_fit, (2, (H - poke_fit.size[1]) // 2))
    canvas.paste(title_fit, (VER_X + 2, 1))
    canvas.paste(ver_fit, (VER_X + 4, 36))
    write_indexed(canvas, crush_dark=True)


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "src",
        nargs="?",
        default=str(ROOT / "testing/game_title_logo.png"),
        help="Source PNG (default: testing/game_title_logo.png)",
    )
    parser.add_argument(
        "--relayout",
        action="store_true",
        help="Split/scale the version strip (destroys pixel layout)",
    )
    args = parser.parse_args()
    if args.relayout:
        pack_relayout()
    else:
        pack_keep_layout(args.src)


if __name__ == "__main__":
    main()
