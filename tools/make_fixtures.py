#!/usr/bin/env python3
"""Builds the fake Friday Night Funkin' mods the desktop tests read.

Two of them, because the two cases the loader has to survive are different:

  fixtures/mod/    a Psych Engine mod, laid out the way one really is, with a
                   character file, a trimmed spritesheet and a health icon.
  fixtures/bare/   a loose PNG and XML with no character file at all, which is
                   what the spritesheet generators on GameBanana hand you and
                   what most people will drop in first.

No Pillow: the PNGs are written here with zlib alone, which every Python has.
The fixtures are checked in, so this only runs when one is missing — but it has
to keep working on a machine that has nothing installed, the same as the plugin.
"""

import os
import json
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "tests", "fixtures")


def write_png(path, width, height, pixels):
    """pixels: a bytes object of RGBA, width*height*4 long."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw += pixels[y * width * 4:(y + 1) * width * 4]

    def chunk(tag, data):
        out = struct.pack(">I", len(data)) + tag + data
        return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def solid_sheet(width, height, blocks):
    """blocks: (x, y, w, h, r, g, b) rectangles on a transparent sheet.

    Every frame gets its own colour so a test can check that the loader cut the
    right rectangle out of the sheet, not merely a rectangle of the right size.
    """
    buf = bytearray(width * height * 4)
    for (x, y, w, h, r, g, b) in blocks:
        for yy in range(y, min(y + h, height)):
            for xx in range(x, min(x + w, width)):
                i = (yy * width + xx) * 4
                buf[i:i + 4] = bytes((r, g, b, 255))
    return bytes(buf)


def sub_texture(name, x, y, w, h, fx=None, fy=None, fw=None, fh=None):
    attrs = f'name="{name}" x="{x}" y="{y}" width="{w}" height="{h}"'
    if fx is not None:
        attrs += f' frameX="{fx}" frameY="{fy}" frameWidth="{fw}" frameHeight="{fh}"'
    return f"    <SubTexture {attrs}/>"


def psych_mod():
    root = os.path.join(OUT, "mod")
    img_dir = os.path.join(root, "images", "characters")

    # Six frames of 20x30 in a row. Two idle, one per arrow. The sing frames are
    # deliberately *trimmed*: a 12x18 rectangle out of a 20x30 frame, offset, so
    # that a loader which ignores frameX/frameY draws them in the wrong place and
    # the test catches it.
    blocks = []
    for i in range(6):
        colour = [(200, 40, 40), (200, 90, 40), (40, 200, 40),
                  (40, 200, 200), (40, 40, 200), (200, 40, 200)][i]
        w, h = (20, 30) if i < 2 else (12, 18)
        blocks.append((i * 20, 0, w, h, *colour))
    write_png(os.path.join(img_dir, "DADDY_DEAREST.png"),
              120, 30, solid_sheet(120, 30, blocks))

    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<TextureAtlas imagePath="DADDY_DEAREST.png">']
    lines.append(sub_texture("Dad idle dance0000", 0, 0, 20, 30))
    lines.append(sub_texture("Dad idle dance0001", 20, 0, 20, 30))
    for i, arrow in enumerate(["LEFT", "DOWN", "UP", "RIGHT"]):
        lines.append(sub_texture(f"Dad Sing Note {arrow}0000", (i + 2) * 20, 0,
                                 12, 18, -4, -6, 20, 30))
    lines.append("</TextureAtlas>")
    with open(os.path.join(img_dir, "DADDY_DEAREST.xml"), "w") as f:
        f.write("\n".join(lines) + "\n")

    character = {
        "animations": [
            {"anim": "idle", "name": "Dad idle dance", "fps": 24,
             "loop": False, "indices": [], "offsets": [0, 0]},
            {"anim": "singLEFT", "name": "Dad Sing Note LEFT", "fps": 24,
             "loop": False, "indices": [], "offsets": [-10, 4]},
            {"anim": "singDOWN", "name": "Dad Sing Note DOWN", "fps": 24,
             "loop": False, "indices": [], "offsets": [0, -20]},
            {"anim": "singUP", "name": "Dad Sing Note UP", "fps": 24,
             "loop": False, "indices": [], "offsets": [0, 20]},
            {"anim": "singRIGHT", "name": "Dad Sing Note RIGHT", "fps": 24,
             "loop": False, "indices": [], "offsets": [10, 4]},
        ],
        "no_antialiasing": False,
        "image": "characters/DADDY_DEAREST",
        "position": [0, 0],
        "healthicon": "dad",
        "flip_x": False,
        "healthbar_colors": [175, 102, 6],
        "camera_position": [0, 0],
        "sing_duration": 6.1,
        "scale": 1,
    }
    char_dir = os.path.join(root, "characters")
    os.makedirs(char_dir, exist_ok=True)
    with open(os.path.join(char_dir, "dad.json"), "w") as f:
        json.dump(character, f, indent=2)

    # A song file in the same mod, to prove the scan does not mistake one for a
    # character: mods are full of JSON that is not a character.
    with open(os.path.join(char_dir, "..", "song-notes.json"), "w") as f:
        json.dump({"song": {"notes": [], "bpm": 150}}, f)

    icon = solid_sheet(60, 30, [(0, 0, 30, 30, 250, 200, 50),
                                (30, 0, 30, 30, 120, 60, 60)])
    write_png(os.path.join(root, "images", "icons", "icon-dad.png"), 60, 30, icon)


def bare_sheet():
    """No character file. The animations have to be guessed out of the sheet.

    The names here are the trap on purpose: "BF NOTE UP" is a prefix of
    "BF NOTE UP MISS", so anything that matches prefixes rather than grouping
    whole names folds the miss frames into the sing.
    """
    root = os.path.join(OUT, "bare")
    blocks = [(i * 16, 0, 16, 24, 20 + i * 30, 100, 200) for i in range(7)]
    write_png(os.path.join(root, "BOYFRIEND.png"), 112, 24,
              solid_sheet(112, 24, blocks))

    names = ["BF idle dance0000", "BF idle dance0001",
             "BF NOTE UP0000", "BF NOTE UP0001",
             "BF NOTE UP MISS0000",
             "BF NOTE LEFT0000", "BF NOTE LEFT0001"]
    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<TextureAtlas imagePath="BOYFRIEND.png">']
    for i, n in enumerate(names):
        lines.append(sub_texture(n, i * 16, 0, 16, 24))
    lines.append("</TextureAtlas>")
    with open(os.path.join(root, "BOYFRIEND.xml"), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    psych_mod()
    bare_sheet()
    print(f"-- fixtures written to {OUT}")


if __name__ == "__main__":
    main()
