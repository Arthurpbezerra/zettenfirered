#!/usr/bin/env python3
"""Generate follower overworld headers for all Gen 1 species from pokeemerald template."""
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EMERALD = ROOT.parent / "pokeemerald"
SPECIES_H = ROOT / "include/constants/species.h"
EMERALD_INFO = EMERALD / "src/data/object_events/object_event_graphics_info_followers.h"
EMERALD_PIC = EMERALD / "src/data/object_events/object_event_pic_tables.h"
SRC_PNG = EMERALD / "graphics/object_events/pics/pokemon"
DST_PNG = ROOT / "graphics/object_events/pics/pokemon/overworld"

OUT_GRAPHICS = ROOT / "src/data/object_events/object_event_graphics_followers.h"
OUT_PIC = ROOT / "src/data/object_events/object_event_pic_tables_followers.h"
OUT_INFO = ROOT / "src/data/object_events/object_event_graphics_info_followers.h"

SPECIAL_CNAME = {
    "Nidoran_f": "NidoranF",
    "Nidoran_m": "NidoranM",
    "Mr_Mime": "MrMime",
    "Ho_oh": "HoOh",
}


def pic_name_to_cname(pic: str) -> str:
    if pic in SPECIAL_CNAME:
        return SPECIAL_CNAME[pic]
    parts = pic.split("_")
    return "".join(p[:1].upper() + p[1:] for p in parts)


def pic_name_to_file(pic: str) -> str:
    return "_".join(p.lower() for p in pic.split("_"))


def parse_species_limit():
    text = SPECIES_H.read_text(encoding="utf-8")
    species = {}
    for m in re.finditer(r"#define SPECIES_(\w+)\s+(\d+)", text):
        name, num = m.group(1), int(m.group(2))
        if name == "EGG":
            continue
        species[num] = name
    max_id = max(n for n in species if n <= 151)
    return {n: species[n] for n in range(0, max_id + 1)}


def parse_emerald_info():
    text = EMERALD_INFO.read_text(encoding="utf-8")
    entries = {}
    for m in re.finditer(
        r"\[SPECIES_(\w+)\]\s*=\s*\{[^}]+sPicTable_(\w+)", text
    ):
        entries[m.group(1)] = m.group(2)
    return entries


def extract_pic_table(pic_name: str) -> str:
    text = EMERALD_PIC.read_text(encoding="utf-8")
    pattern = rf"static const struct SpriteFrameImage sPicTable_{pic_name}\[\] = \{{(.*?)}};"
    m = re.search(pattern, text, re.DOTALL)
    if not m:
        raise SystemExit(f"Missing sPicTable_{pic_name}")
    body = m.group(1)
    cname = pic_name_to_cname(pic_name)
    body = body.replace(f"gObjectEventPic_{pic_name}", f"gObjectEventPic_OW_{cname}")
    return (
        f"static const struct SpriteFrameImage sPicTable_OW_{cname}[] = {{{body}}};"
    )


def main():
    species_map = parse_species_limit()
    emerald_pic = parse_emerald_info()

    DST_PNG.mkdir(parents=True, exist_ok=True)

    graphics_lines = [
        "// Follower overworld gfx. Paths use overworld/ to avoid clashing with vanilla story sprites.",
        "#if !(OW_GFX_COMPRESS)",
        "#define INCBIN_COMP INCBIN_U32",
        "#endif",
        "",
    ]
    pic_lines = ["// Follower overworld pic tables (4x4 frames, OW_* gfx paths)."]
    info_lines = [
        "// Set .compressed = OW_GFX_COMPRESS",
        "#define COMP OW_GFX_COMPRESS",
        "",
        "// Species-indexed follower gfx. Unlisted species fall back to substitute.",
        "const struct ObjectEventGraphicsInfo gPokemonObjectGraphics[] = {",
    ]

    copied = 0
    for num in sorted(species_map.keys()):
        sp_const = species_map[num]
        if sp_const == "NONE":
            pic = "Substitute"
        else:
            if sp_const not in emerald_pic:
                raise SystemExit(f"No emerald entry for SPECIES_{sp_const}")
            pic = emerald_pic[sp_const]

        cname = pic_name_to_cname(pic)
        file_base = pic_name_to_file(pic)

        src = SRC_PNG / f"{file_base}.png"
        dst = DST_PNG / f"{file_base}.png"
        if not src.exists():
            raise SystemExit(f"Missing PNG: {src}")
        if not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime:
            shutil.copy2(src, dst)
            copied += 1

        graphics_lines.append(
            f'const u32 gObjectEventPic_OW_{cname}[] = INCBIN_COMP("graphics/object_events/pics/pokemon/overworld/{file_base}.4bpp");'
        )

        pic_lines.append(extract_pic_table(pic))

        # Read full info line from emerald for tracks etc.
        info_text = EMERALD_INFO.read_text(encoding="utf-8")
        m = re.search(
            rf"\[SPECIES_{sp_const}\]\s*=\s*(\{{[^}}]+\}}),",
            info_text,
        )
        if not m:
            raise SystemExit(f"Missing info for SPECIES_{sp_const}")
        body = m.group(1)
        body = body.replace("sOamTables_32x32", "gObjectEventSpriteOamTables_32x32")
        body = re.sub(
            r"sPicTable_\w+",
            f"sPicTable_OW_{cname}",
            body,
        )
        info_lines.append(f"    [SPECIES_{sp_const}] = {body},")

    graphics_lines.extend(
        [
            "",
            "const u16 gObjectEventPal_OW_Substitute[] = INCBIN_U16(\"graphics/object_events/pics/pokemon/overworld/substitute.gbapal\");",
            "",
        ]
    )
    info_lines.append("};")
    info_lines.append("")

    OUT_GRAPHICS.write_text("\n".join(graphics_lines), encoding="utf-8", newline="\n")
    OUT_PIC.write_text("\n".join(pic_lines) + "\n", encoding="utf-8", newline="\n")
    OUT_INFO.write_text("\n".join(info_lines), encoding="utf-8", newline="\n")

    print(f"Generated {len(species_map)} species, copied/updated {copied} PNGs")
    print(f"  {OUT_GRAPHICS}")
    print(f"  {OUT_PIC}")
    print(f"  {OUT_INFO}")


if __name__ == "__main__":
    main()
