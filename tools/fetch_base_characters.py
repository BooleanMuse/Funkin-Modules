#!/usr/bin/env python3
"""Builds a Psych Engine mod folder out of the base game's own assets.

Why this exists. The spritesheets for Dad, Mom and Pico are in the public
Friday Night Funkin' repository, but the version they are in is older than
character files: it hardcodes every character in Haxe. So the plugin can open
those sheets, but it has to guess the animations off the frame names, and it
gets no offsets, no sing durations and no health icon.

All three of those are in the same repository, just not in the form we want:

  - the animation names, prefixes and offsets are in `source/Character.hx`,
    which is where this script's tables were read from, by hand and by eye;
  - the icons are one grid image, `iconGrid.png`, and which cell belongs to
    which character is in `source/HealthIcon.hx`. Do NOT guess this by looking
    at the grid: pico and dad are easy to swap, and swapping them is exactly
    what happened the first time.

So: download the sheets, cut the icons out of the grid, and write the character
files this plugin (and Psych Engine, and any other engine) would want.

    python3 tools/fetch_base_characters.py [destination]

Default destination is the plugin's own characters folder. Nothing is shipped
with the plugin — these are the game's assets, downloaded to your own machine.

No Pillow and no other library: the PNG in and out is done here with zlib,
which every Python has. A tool that says "first go and install something" is
the thing this whole project refuses to do.
"""

import os
import json
import struct
import sys
import urllib.request
import zlib

BASE = "https://raw.githubusercontent.com/FunkinCrew/Funkin/v0.2.7.1/assets/images"

# ---------------------------------------------------------------------------
# PNG, both ways, with nothing but zlib
# ---------------------------------------------------------------------------


def read_png(data):
    """-> (width, height, bytearray of RGBA). 8-bit, non-interlaced, colour type
    2 (RGB) or 6 (RGBA), which is everything the game ships."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = 0
    channels = 4
    idat = bytearray()
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace != 0 or colour not in (2, 6):
                raise ValueError("unsupported PNG: depth %d colour %d interlace %d"
                                 % (depth, colour, interlace))
            channels = 4 if colour == 6 else 3
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        # The five PNG filters. Each pixel is predicted from its neighbours and
        # only the difference is stored, so every row has to be undone in order.
        if filt == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif filt != 0:
            raise ValueError("unknown PNG filter %d" % filt)
        prev = line

        for x in range(width):
            s = x * channels
            d = (y * width + x) * 4
            out[d] = line[s]
            out[d + 1] = line[s + 1]
            out[d + 2] = line[s + 2]
            out[d + 3] = line[s + 3] if channels == 4 else 255
    return width, height, out


def write_png(path, width, height, rgba):
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw += rgba[y * width * 4:(y + 1) * width * 4]

    def chunk(tag, body):
        out = struct.pack(">I", len(body)) + tag + body
        return out + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def crop(src, src_w, x0, y0, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        s = ((y0 + y) * src_w + x0) * 4
        d = y * w * 4
        out[d:d + w * 4] = src[s:s + w * 4]
    return out


# ---------------------------------------------------------------------------
# What the game says about its own characters
# ---------------------------------------------------------------------------

# Straight out of source/HealthIcon.hx. The grid is ten cells across, and a
# character is one or two of them: the cheerful one and the losing one.
ICON_CELLS = {
    "bf": [0, 1],
    "spooky": [2, 3],
    "pico": [4, 5],
    "mom": [6, 7],
    "tankman": [8, 9],
    "face": [10, 11],
    "dad": [12, 13],
    "bf-old": [14, 15],
    "gf": [16],
    "monster": [19, 20],
}

# Straight out of source/Character.hx: the prefix each animation is built from,
# and the offset it is drawn with. The offsets are the reason this is worth
# doing at all — without them a character's head jumps half a body sideways
# between its idle and its sing.
CHARACTERS = [
    {
        "name": "dad",
        "sheet": "DADDY_DEAREST",
        "icon": "dad",
        "bar": [175, 102, 6],
        "flip_x": False,
        "anims": [
            ("idle", "Dad idle dance", [0, 0]),
            ("singUP", "Dad Sing Note UP", [-6, 50]),
            ("singRIGHT", "Dad Sing Note RIGHT", [0, 27]),
            ("singDOWN", "Dad Sing Note DOWN", [0, -30]),
            ("singLEFT", "Dad Sing Note LEFT", [-10, 10]),
        ],
    },
    {
        "name": "mom",
        "sheet": "Mom_Assets",
        "icon": "mom",
        "bar": [212, 46, 143],
        "flip_x": False,
        # The two side prefixes really are the wrong way round in the sheet, and
        # the game's own source says so in capitals. Reading the frame names and
        # believing them gives a mother who reaches the wrong way all song.
        "anims": [
            ("idle", "Mom Idle", [0, 0]),
            ("singUP", "Mom Up Pose", [14, 71]),
            ("singRIGHT", "Mom Pose Left", [10, -60]),
            ("singDOWN", "MOM DOWN POSE", [20, -160]),
            ("singLEFT", "Mom Left Pose", [250, -23]),
        ],
    },
    {
        "name": "pico",
        "sheet": "Pico_FNF_assetss",
        "icon": "pico",
        "bar": [178, 220, 55],
        # Pico is drawn facing the other way and the game flips him, which is
        # also why his left and right prefixes look swapped: they are written
        # for the side he ends up on.
        "flip_x": True,
        "anims": [
            ("idle", "Pico Idle Dance", [0, 0]),
            ("singUP", "pico Up note0", [-29, 27]),
            ("singRIGHT", "Pico Note Right0", [-68, -7]),
            ("singDOWN", "Pico Down Note0", [200, -70]),
            ("singLEFT", "Pico NOTE LEFT0", [65, 9]),
            ("singUPmiss", "pico Up note miss", [-19, 67]),
            ("singRIGHTmiss", "Pico Note Right Miss", [-60, 41]),
            ("singDOWNmiss", "Pico Down Note MISS", [210, -28]),
            ("singLEFTmiss", "Pico NOTE LEFT miss", [62, 64]),
        ],
    },
]


def fetch(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        print("   have  %s" % os.path.basename(path))
        return open(path, "rb").read()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    print("   get   %s" % os.path.basename(path))
    with urllib.request.urlopen(url, timeout=120) as r:
        data = r.read()
    with open(path, "wb") as f:
        f.write(data)
    return data


def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/.local/share/Rack2/FunkinRack/characters/FNF base game (Psych)")
    print("-- %s" % dest)

    for c in CHARACTERS:
        fetch("%s/%s.png" % (BASE, c["sheet"]),
              os.path.join(dest, "images", "characters", c["sheet"] + ".png"))
        fetch("%s/%s.xml" % (BASE, c["sheet"]),
              os.path.join(dest, "images", "characters", c["sheet"] + ".xml"))

    grid_path = os.path.join(dest, "iconGrid.png")
    grid = fetch("%s/iconGrid.png" % BASE, grid_path)
    gw, gh, pixels = read_png(grid)
    cell = 150
    across = gw // cell
    print("   icons from a %dx%d grid" % (across, gh // cell))

    for c in CHARACTERS:
        cells = ICON_CELLS[c["icon"]]
        strip = bytearray(cell * len(cells) * cell * 4)
        for i, n in enumerate(cells):
            x, y = (n % across) * cell, (n // across) * cell
            tile = crop(pixels, gw, x, y, cell, cell)
            for row in range(cell):
                s = row * cell * 4
                d = (row * cell * len(cells) + i * cell) * 4
                strip[d:d + cell * 4] = tile[s:s + cell * 4]
        write_png(os.path.join(dest, "images", "icons", "icon-%s.png" % c["icon"]),
                  cell * len(cells), cell, strip)
        print("   icon  icon-%s.png  (cells %s)" % (c["icon"], cells))

    for c in CHARACTERS:
        character = {
            "animations": [
                {"anim": anim, "name": prefix, "fps": 24, "loop": False,
                 "indices": [], "offsets": offsets}
                for (anim, prefix, offsets) in c["anims"]
            ],
            "no_antialiasing": False,
            "image": "characters/" + c["sheet"],
            "position": [0, 0],
            "healthicon": c["icon"],
            "flip_x": c["flip_x"],
            "healthbar_colors": c["bar"],
            "camera_position": [0, 0],
            "sing_duration": 4,
            "scale": 1,
        }
        path = os.path.join(dest, "characters", c["name"] + ".json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(character, f, indent=2)
        print("   char  characters/%s.json" % c["name"])

    # The grid itself is not part of the mod; it was only a bag of icons.
    os.remove(grid_path)
    print("-- done. Right-click a Funkin' module to pick one.")


if __name__ == "__main__":
    main()
