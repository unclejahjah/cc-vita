# sheet.py frame frame ...: contact sheet of as/a_NNN.png frames (2 columns, half size)
import sys
from PIL import Image
S = '/mnt/c/Users/Jah/AppData/Local/Temp/claude/C--Users-Jah--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad/as/'
fr = [int(x) for x in sys.argv[1:]]
ims = [Image.open(S + 'a_%03d.png' % i) for i in fr]
w, h = ims[0].size
rows = (len(ims) + 1) // 2
o = Image.new('RGB', (w, h // 2 * rows))
for k, im in enumerate(ims):
    o.paste(im.resize((w // 2, h // 2)), ((k % 2) * w // 2, (k // 2) * h // 2))
o.save(S + 'sheet.png')
