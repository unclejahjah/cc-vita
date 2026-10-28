#!/usr/bin/env python3
"""MODEART.swf: the pictures Local Game's mode book borrows from other files.

  python3 make_modeart.py PS3_MENU.swf PC_QUOFF.swf MODEART.swf

- "volleyball": the PS3 MENU.swf mode book's Volleyball picture (shape 548, its top-left page)
- "quaff": QUOFF.swf's All You Can Quaff banner (sprite "aycqselect", 22), without its card

Each is a sprite of its own, exported by that name, with the characters it uses copied over
under new ids. Made from your own files; the output is game art, never commit it.
"""
import struct
import sys
import zlib

SHAPES = {2, 22, 32, 83}
SPRITE = 39
PLACES = {4, 26, 70}


def read(path):
    d = open(path, 'rb').read()
    if d[:3] == b'CWS':
        d = b'FWS' + d[3:8] + zlib.decompress(d[8:])
    nbits = d[8] >> 3
    return d, 8 + (5 + nbits * 4 + 7) // 8 + 4


def tags(d, off, end):
    while off < end:
        h = struct.unpack_from('<H', d, off)
        off += 2
        code, ln = h[0] >> 6, h[0] & 63
        if ln == 63:
            ln = struct.unpack_from('<I', d, off)[0]
            off += 4
        yield code, d[off:off + ln]
        off += ln
        if code == 0:
            break


def tag(code, body):
    if len(body) < 63 and code not in SHAPES | {SPRITE}:
        return struct.pack('<H', code << 6 | len(body)) + body
    return struct.pack('<HI', code << 6 | 63, len(body)) + body


class Source:
    def __init__(self, path):
        d, off = read(path)
        self.defs = {}
        for code, body in tags(d, off, len(d)):
            if len(body) >= 2 and (code in SHAPES or code == SPRITE):
                self.defs[struct.unpack_from('<H', body)[0]] = (code, body)


def copy(src, cid, out, ids, skip_depths=()):
    """cid and what it places (but not at skip_depths), into out (tags) with new ids from ids;
    returns the new id"""
    if (id(src), cid) in ids:
        return ids[(id(src), cid)]
    code, body = src.defs[cid]
    if code == SPRITE:
        parts = b''
        for c2, b2 in tags(body, 4, len(body)):
            if c2 == 26 and struct.unpack_from('<H', b2, 1)[0] in skip_depths:
                continue
            if c2 == 26 and b2[0] & 2:   # PlaceObject2 with a character: its new id
                new = copy(src, struct.unpack_from('<H', b2, 3)[0], out, ids)
                b2 = b2[:3] + struct.pack('<H', new) + b2[5:]
            elif c2 in (4, 70):
                sys.exit('PlaceObject/PlaceObject3 in sprite %d: not handled' % cid)
            parts += struct.pack('<HI', c2 << 6 | 63, len(b2)) + b2
        new = len(ids) + 1
        ids[(id(src), cid)] = new
        out.append(tag(SPRITE, struct.pack('<H', new) + body[2:4] + parts))
        return new
    new = len(ids) + 1   # a shape (its fills are plain colours here: no bitmaps to bring)
    ids[(id(src), cid)] = new
    out.append(tag(code, struct.pack('<H', new) + body[2:]))
    return new


def wrap_shape(shape_id, out, ids):
    """a one-frame sprite showing that shape at depth 1"""
    new = len(ids) + 1
    ids[('wrap', shape_id)] = new
    place = bytes([0x02]) + struct.pack('<HH', 1, shape_id)   # PlaceObject2: character, depth 1
    body = struct.pack('<HH', new, 1) + struct.pack('<HI', 26 << 6 | 63, len(place)) + place
    body += struct.pack('<H', 1 << 6) + struct.pack('<H', 0)   # ShowFrame, End
    out.append(tag(SPRITE, body))
    return new


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    ps3, quoff = Source(sys.argv[1]), Source(sys.argv[2])
    out, ids = [], {}
    volley = wrap_shape(copy(ps3, 548, out, ids), out, ids)
    quaff = copy(quoff, 22, out, ids, skip_depths=(1,))   # without its tan card (depth 1)
    exports = struct.pack('<H', 2)
    for cid, name in ((volley, b'volleyball'), (quaff, b'quaff')):
        exports += struct.pack('<H', cid) + name + b'\0'
    body = b''.join(out) + tag(56, exports) + struct.pack('<H', 1 << 6) + struct.pack('<H', 0)
    rect = bytes([0x00])   # an empty stage (nbits 0)
    header = rect + struct.pack('<HH', 30 << 8, 1)
    swf = b'FWS' + bytes([8]) + struct.pack('<I', 8 + len(header) + len(body)) + header + body
    open(sys.argv[3], 'wb').write(swf)
    print('%s: %d bytes, %d characters' % (sys.argv[3], len(swf), len(ids)))


main()
