# Summarise SWF structure: tag histogram, root timeline, exports, imports.
import sys, struct, collections

NAMES = {0:'End',1:'ShowFrame',2:'DefShape',4:'Place',5:'Remove',6:'DefBits',8:'JPEGTables',9:'BgColor',
 10:'DefFont',11:'DefText',12:'DoAction',14:'DefSound',15:'StartSound',18:'SoundStreamHead',19:'SoundStreamBlock',
 20:'DefBitsLossless',21:'DefBitsJPEG2',22:'DefShape2',26:'Place2',28:'Remove2',32:'DefShape3',33:'DefText2',
 34:'DefButton2',35:'DefBitsJPEG3',36:'DefBitsLossless2',37:'DefEditText',39:'DefSprite',43:'FrameLabel',
 45:'SoundStreamHead2',46:'DefMorphShape',48:'DefFont2',56:'ExportAssets',57:'ImportAssets',59:'DoInitAction',
 62:'DefFontInfo2',69:'FileAttributes',70:'Place3',71:'ImportAssets2',73:'DefFontAlignZones',74:'CSMTextSettings',
 75:'DefFont3',77:'Metadata',78:'Scale9',83:'DefShape4',84:'DefMorphShape2',86:'DefSceneLabel',88:'DefFontName'}

def tags(d, pos, end):
    end = min(end, len(d))
    while pos + 2 <= end:
        h = struct.unpack_from('<H', d, pos)[0]; pos += 2
        code, l = h >> 6, h & 0x3f
        if l == 0x3f:
            if pos + 4 > end: return
            l = struct.unpack_from('<I', d, pos)[0]; pos += 4
        yield code, pos, l
        if code == 0: return
        pos += l

def cstr(d, p):
    e = d.index(b'\0', p); return d[p:e].decode('latin1'), e + 1

for path in sys.argv[1:]:
    d = open(path, 'rb').read()
    nbits = d[8] >> 3
    rb = (5 + 4 * nbits + 7) // 8
    xs = []
    bits = int.from_bytes(d[8:8 + rb], 'big')
    tot = rb * 8
    vals = [(bits >> (tot - 5 - nbits * (i + 1))) & ((1 << nbits) - 1) for i in range(4)]
    vals = [v - (1 << nbits) if v >> (nbits - 1) else v for v in vals]
    start = 8 + rb + 4
    fps = d[8 + rb + 1] + d[8 + rb] / 256
    print('=' * 70); print(path.split('/')[-1], 'stage %dx%d px  fps %.1f  size %d (header %d)' %
          ((vals[1] - vals[0]) // 20, (vals[3] - vals[2]) // 20, fps, len(d), struct.unpack_from('<I', d, 4)[0]))
    hist = collections.Counter(); root_frames = 0; root_labels = []; exports = []; imports = []
    sprite_hist = collections.Counter(); places_root = 0; actions = 0
    for code, p, l in tags(d, start, len(d)):
        hist[code] += 1
        if code == 1: root_frames += 1
        elif code == 43: root_labels.append((cstr(d, p)[0], root_frames))
        elif code in (26, 70): places_root += 1
        elif code == 56:
            n = struct.unpack_from('<H', d, p)[0]; q = p + 2
            for _ in range(n):
                cid = struct.unpack_from('<H', d, q)[0]; name, q = cstr(d, q + 2); exports.append((cid, name))
        elif code in (57, 71):
            url, q = cstr(d, p); imports.append(url)
        elif code == 39:
            for c2, _, _ in tags(d, p + 4, p + l): sprite_hist[c2] += 1
    print(' tags:', ', '.join('%s=%d' % (NAMES.get(c, c), n) for c, n in hist.most_common()))
    print(' in sprites:', ', '.join('%s=%d' % (NAMES.get(c, c), n) for c, n in sprite_hist.most_common()))
    print(' root: %d frames, %d places, labels %s' % (root_frames, places_root, root_labels[:20]))
    print(' imports:', imports[:5])
    print(' exports (%d):' % len(exports), [n for _, n in exports[:40]])
