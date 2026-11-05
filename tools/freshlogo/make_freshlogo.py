#!/usr/bin/env python3
"""FRESHLOGO.swf: the PC title's "FRESH*" sticker, shown on the Vita title while the FRESH look is on.

  python3 make_freshlogo.py PC_MENU.swf FRESHLOGO.swf

PC_MENU.swf is the PC game's newer MENU.swf (its menu.pak, decrypted as for tools/paintjr). The
sticker is shape 735 there, frame 1 of sprite 736 on the Play button of "buttonchoices" (sprite
737, depth 4; its other frames are a text field); it's exported here as "fresh", a one-frame
sprite showing the shape. Made from your own
files; the output is game art, never commit it.
"""
import struct
import sys
import zlib

SHAPES = {2, 22, 32, 83}
SPRITE = 39
STICKER = 735


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


def copy(defs, cid, out, ids):
    """cid and what it places, into out (tags) with new ids; returns the new id"""
    if cid in ids:
        return ids[cid]
    code, body = defs[cid]
    if code == SPRITE:
        parts = b''
        for c2, b2 in tags(body, 4, len(body)):
            if c2 == 26 and b2[0] & 2:   # PlaceObject2 with a character: its new id
                b2 = b2[:3] + struct.pack('<H', copy(defs, struct.unpack_from('<H', b2, 3)[0], out, ids)) + b2[5:]
            elif c2 in (4, 70):
                sys.exit('PlaceObject/PlaceObject3 in sprite %d: not handled' % cid)
            parts += struct.pack('<HI', c2 << 6 | 63, len(b2)) + b2
        new = ids[cid] = len(ids) + 1
        out.append(tag(SPRITE, struct.pack('<H', new) + body[2:4] + parts))
        return new
    if code not in SHAPES:
        sys.exit('character %d is tag %d: not handled' % (cid, code))
    new = ids[cid] = len(ids) + 1
    out.append(tag(code, struct.pack('<H', new) + body[2:]))
    return new


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d, off = read(sys.argv[1])
    defs = {}
    for code, body in tags(d, off, len(d)):
        if len(body) >= 2 and (code in SHAPES or code == SPRITE):
            defs[struct.unpack_from('<H', body)[0]] = (code, body)
    if STICKER not in defs or defs[STICKER][0] not in SHAPES:
        sys.exit('no shape %d: not the PC\'s newer MENU.swf?' % STICKER)
    out, ids = [], {}
    shape = copy(defs, STICKER, out, ids)
    fresh = ids['sprite'] = len(ids) + 1   # a one-frame sprite showing the shape at depth 1
    place = bytes([0x02]) + struct.pack('<HH', 1, shape)
    body = struct.pack('<HH', fresh, 1) + struct.pack('<HI', 26 << 6 | 63, len(place)) + place
    out.append(tag(SPRITE, body + struct.pack('<H', 1 << 6) + struct.pack('<H', 0)))
    exports = struct.pack('<H', 1) + struct.pack('<H', fresh) + b'fresh\0'
    body = b''.join(out) + tag(56, exports) + struct.pack('<H', 1 << 6) + struct.pack('<H', 0)
    header = bytes([0x00]) + struct.pack('<HH', 30 << 8, 1)   # an empty stage, 30 fps, 1 frame
    swf = b'FWS' + bytes([8]) + struct.pack('<I', 8 + len(header) + len(body)) + header + body
    open(sys.argv[2], 'wb').write(swf)
    print('%s: %d bytes, %d characters' % (sys.argv[2], len(swf), len(ids)))


main()
