#!/usr/bin/env python3
# The custom animal orb's template (src/custom_animal.h), drawn from nothing: a 1024x1024 PNG
# with a framed box to paint the pet in. Saved twice: template.png to paint on, and stencil.png,
# the blank copy the game compares against (pixels still equal to it are guides: cleared).
# The box must match BOX_X0..BOX_Y1 in src/custom_animal.c.
#   make_template.py OUT_DIR
import os, sys
from PIL import Image, ImageDraw, ImageFont

SIZE, X0, Y0, X1, Y1 = 1024, 136, 136, 888, 888   # inside of the frame
FRAME = 8
PAPER, LINE, GUIDE, GROUND = (236, 228, 212, 255), (0, 0, 0, 255), (200, 200, 200, 255), (110, 160, 230, 255)

def font(size):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()

im = Image.new("RGBA", (SIZE, SIZE), PAPER)
d = ImageDraw.Draw(im)
d.rectangle((X0 - FRAME, Y0 - FRAME, X1 + FRAME - 1, Y1 + FRAME - 1), fill=LINE)
d.rectangle((X0, Y0, X1 - 1, Y1 - 1), fill=(0, 0, 0, 0))
# guides (not antialiased, so the stencil matches them exactly): centre line, ground line
cx, ground = (X0 + X1) // 2, Y1 - 60
for y in range(Y0, Y1, 24):
    d.line((cx, y, cx, min(y + 12, Y1 - 1)), fill=GUIDE, width=2)
d.line((X0, ground, X1 - 1, ground), fill=GROUND, width=3)
d.text((SIZE // 2, 60), "CUSTOM ANIMAL ORB", font=font(44), fill=LINE, anchor="mm")
d.text((SIZE // 2, 104), "paint your pet inside the box, feet on the blue line", font=font(24), fill=LINE, anchor="mm")
d.text((SIZE // 2, Y1 + 44), "keep the background transparent, outlines bold", font=font(24), fill=LINE, anchor="mm")
d.text((SIZE // 2, Y1 + 84), "save as painting.png in ux0:data/CastleCrashers/custom/animal/", font=font(20), fill=LINE, anchor="mm")
out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)
im.save(os.path.join(out, "template.png"))
im.save(os.path.join(out, "stencil.png"))
