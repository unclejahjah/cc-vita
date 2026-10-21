#!/usr/bin/env python3
"""LiveArea art from renders of the user's own game files (nothing here is committed or shipped):
  make_livearea.py RENDERS FONTS OUT VERSION [BACKGROUND]
RENDERS holds scene.png (MENU.swf's title scene without logo or news bar, 1920x1088), logo.png
(the logo, transparent) and behemoth.png (LEGAL.swf's bird, transparent),
as make_livearea.sh renders them. FONTS holds the game's synjUnicode_20 font. Writes OUT/sce_sys/:
  icon0.png                      128x128, the Orange Knight
  livearea/contents/bg.png       840x500, the title scene + version (bottom left) + The Behemoth (bottom right)
  livearea/contents/startup.png  420x237, the logo (template.xml's tap-to-start image, centred)
BACKGROUND: a picture of your own to use in place of the title scene (scaled to 500 high, centre cropped).
Layout as the Punch Quest port's LiveArea; every PNG is 8-bit indexed as the Vita wants."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmfont
from PIL import Image, ImageFilter

src, fonts, out, version = sys.argv[1:5]
own_bg = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] else None

def outline(im, r, col=(0, 0, 0)):
    a = im.split()[3].filter(ImageFilter.MaxFilter(2 * r + 1))
    o = Image.new('RGBA', im.size, col + (255,))
    o.putalpha(a)
    o.alpha_composite(im)
    return o

def pad(im, r):
    p = Image.new('RGBA', (im.width + 2 * r, im.height + 2 * r))
    p.alpha_composite(im, (r, r))
    return p

def indexed(img, path):
    if img.mode == 'RGBA' and img.getextrema()[3][0] < 255:
        img.quantize(256, method=Image.Quantize.FASTOCTREE).save(path, optimize=True)
    else:
        img.convert('RGB').quantize(256).save(path, optimize=True)

def trimmed(path):
    im = Image.open(path).convert('RGBA')
    return im.crop(im.getbbox())

scene = Image.open(os.path.join(src, 'scene.png')).convert('RGBA')
logo, beh = trimmed(os.path.join(src, 'logo.png')), trimmed(os.path.join(src, 'behemoth.png'))

# bg.png: the scene scaled to 500 high, cropped from the left so the knights stay in
s = 500 / scene.height
bg = scene.resize((round(scene.width * s), 500), Image.LANCZOS)
bg = bg.crop((bg.width - 840, 0, bg.width, 500))
if own_bg:
    o = Image.open(own_bg).convert('RGBA')
    o = o.resize((max(840, round(o.width * 500 / o.height)), 500), Image.LANCZOS)
    bg = o.crop(((o.width - 840) // 2, 0, (o.width - 840) // 2 + 840, 500))
font = bmfont.load(os.path.join(fonts, 'synjUnicode_20.fnt'), os.path.join(fonts, 'synjUnicode_20_0.dds'))
lab = bmfont.text(font, version)
lab = outline(pad(lab.resize((round(lab.width * 26 / lab.height), 26), Image.LANCZOS), 4), 3)
bg.alpha_composite(lab, (20, 480 - lab.height))
b = beh.resize((round(beh.width * 70 / beh.height), 70), Image.LANCZOS)
b = outline(pad(b, 3), 2, (255, 255, 255))
bg.alpha_composite(b, (840 - 20 - b.width, 482 - b.height))

# startup.png: the logo in the title menu's cream highlight glow (cc_native.c's ring/haze colours),
# centred in the image; template.xml moves the image to the centre left
GLOW = (255, 246, 206)
lg = logo.resize((400, round(logo.height * 400 / logo.width)), Image.LANCZOS)
st = Image.new('RGBA', (420, 237))
at = ((420 - lg.width) // 2, (237 - lg.height) // 2)
glow = Image.new('RGBA', st.size)
glow.alpha_composite(lg, at)
a = glow.split()[3].filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(3))
a = a.point(lambda v: min(255, v * 1.6) * 0.85).convert('L')
halo = Image.new('RGBA', st.size, GLOW + (255,))
halo.putalpha(a)
st.alpha_composite(halo)
st.alpha_composite(lg, at)

# icon0.png: the Orange Knight's head (the Vita shows it in a circle)
icon = scene.crop((1405, 562, 1705, 862)).resize((128, 128), Image.LANCZOS)

contents = os.path.join(out, 'sce_sys', 'livearea', 'contents')
os.makedirs(contents, exist_ok=True)
indexed(bg.convert('RGB'), os.path.join(contents, 'bg.png'))
indexed(st, os.path.join(contents, 'startup.png'))
indexed(icon.convert('RGB'), os.path.join(out, 'sce_sys', 'icon0.png'))
print('LiveArea art written to', os.path.join(out, 'sce_sys'))
