# make_trp.py OUT.trp [ICON_DIR]: an unsigned TROPHY.TRP for the NoTrpDrm plugin.
# Holds TROPCONF.SFM, TROP.SFM, ICON0.PNG and TROP000..011.PNG for the 12 achievements
# (same order as ACH in src/cc_host.c, trophy id = achievement id - 1). ICON_DIR may hold
# ICON0.PNG / TROPnnn.PNG of your own; missing icons are drawn here.
import hashlib, io, struct, sys
from xml.sax.saxutils import escape
from PIL import Image, ImageDraw, ImageFont

NPCOMMID = "CCRV00001_00"
TITLE = "Castle Crashers"
# name, detail, grade (B/S/G), hidden
TROPHIES = [
    ("Kay Eye Ess Ess", "Collect all 4 princesses' kisses in a multiplayer game", "B", False),
    ("Traditional", "Complete the game using any character", "G", False),
    ("The Traitor", "Defeat any boss by playing one of his own minions", "S", False),
    ("Deer Trainer", "Navigate the Abandoned Mill without hitting any obstacles", "B", False),
    ("Conscientious Objector", "Complete Home Castle through Barbarian Boss without attacking any foes", "S", False),
    ("Maximum Firepower", "Using a catapult, fling a knight with maximum power", "B", False),
    ("Animal Handler", "Collect all the animal orbs", "S", False),
    ("Arena Master", "Win 40 online Arena matches", "B", False),
    ("Glork", "Win 20 ranked All You Can Quaff matches", "B", False),
    ("Melee Is Best", "Defeat any end boss without using any magic", "B", False),
    ("Treasure Hunter", "Find and dig up ten buried items", "B", False),
    ("Medic!", "In a 4-player game, revive each fallen comrade at least once", "B", False),
]
GRADE_COLOUR = {"B": (196, 124, 64), "S": (200, 205, 215), "G": (240, 196, 60)}


def sfm(with_text):
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<trophyconf version="1.1" platform="psp2">',
           f'<npcommid>{NPCOMMID}</npcommid>',
           '<trophyset-version>01.00</trophyset-version>',
           '<parental-level license-area="default">0</parental-level>',
           f'<title-name>{TITLE}</title-name>',
           f'<title-detail>{TITLE} trophies</title-detail>']
    for i, (name, detail, grade, hidden) in enumerate(TROPHIES):
        out.append(f'<trophy id="{i:03d}" hidden="{"yes" if hidden else "no"}" ttype="{grade}" pid="-1">')
        if with_text:
            out.append(f'<name>{escape(name)}</name><detail>{escape(detail)}</detail>')
        out.append('</trophy>')
    out.append('</trophyconf>')
    return ("\n".join(out) + "\n").encode("utf-8")


def font(size):
    for f in ("DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def png(img):
    b = io.BytesIO()
    img.save(b, "PNG")
    return b.getvalue()


def drawn_icon(name, grade):
    # a shield in the trophy's grade colour with the name's initials
    img = Image.new("RGB", (240, 240), (38, 24, 18))
    d = ImageDraw.Draw(img)
    c = GRADE_COLOUR[grade]
    d.polygon([(40, 30), (200, 30), (200, 130), (120, 215), (40, 130)], fill=c, outline=(0, 0, 0), width=8)
    initials = "".join(w[0] for w in name.replace("!", "").split()[:3]).upper()
    f = font(64 if len(initials) < 3 else 50)
    w = d.textlength(initials, font=f)
    d.text((120 - w / 2, 70), initials, font=f, fill=(0, 0, 0))
    return png(img)


def drawn_title():
    img = Image.new("RGB", (320, 176), (38, 24, 18))
    d = ImageDraw.Draw(img)
    f = font(28)
    for y, t in ((50, "CASTLE"), (95, "CRASHERS")):
        w = d.textlength(t, font=f)
        d.text((160 - w / 2 + 3, y + 3), t, font=f, fill=(0, 0, 0))
        d.text((160 - w / 2, y), t, font=f, fill=(240, 196, 60))
    return png(img)


def own(icon_dir, name):
    if not icon_dir:
        return None
    try:
        with open(f"{icon_dir}/{name}", "rb") as f:
            return f.read()
    except OSError:
        return None


def build(out_path, icon_dir=None):
    files = [("TROPCONF.SFM", sfm(False)), ("TROP.SFM", sfm(True)),
             ("ICON0.PNG", own(icon_dir, "ICON0.PNG") or drawn_title())]
    for i, (name, _, grade, _) in enumerate(TROPHIES):
        files.append((f"TROP{i:03d}.PNG", own(icon_dir, f"TROP{i:03d}.PNG") or drawn_icon(name, grade)))

    # big-endian: 0x40 header, 0x40 per entry, then each file 16-byte aligned
    pos = 0x40 + 0x40 * len(files)
    table, blob = b"", b""
    for name, data in files:
        pad = (-pos) % 16
        blob += b"\0" * pad
        pos += pad
        table += struct.pack(">32sQQI12x", name.encode(), pos, len(data), 0)
        blob += data
        pos += len(data)
    head = struct.pack(">IIQIII20s16x", 0xDCA24D00, 2, pos, len(files), 0x40, 1, b"")
    trp = bytearray(head + table + blob)
    trp[0x1C:0x30] = hashlib.sha1(trp).digest()   # over the whole file with this field zeroed
    with open(out_path, "wb") as f:
        f.write(trp)


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
