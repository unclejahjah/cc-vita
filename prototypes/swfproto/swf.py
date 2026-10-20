"""Minimal SWF reader for Castle Crashers' decompiled assets.

Parses the subset the game uses: shapes (1-4), sprites, PlaceObject2/3,
RemoveObject2, FrameLabel, ExportAssets and lossless bitmaps. Prototype for
the Vita renderer; reads files at runtime, ships no game data.
"""
import struct
import zlib


class Bits:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos * 8

    def align(self):
        self.p = (self.p + 7) & ~7

    @property
    def byte(self):
        self.align()
        return self.p >> 3

    def ub(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def sb(self, n):
        if n == 0:
            return 0
        v = self.ub(n)
        if v & (1 << (n - 1)):
            v -= 1 << n
        return v

    def fb(self, n):
        return self.sb(n) / 65536.0

    def u8(self):
        self.align()
        v = self.d[self.p >> 3]
        self.p += 8
        return v

    def u16(self):
        self.align()
        v = struct.unpack_from('<H', self.d, self.p >> 3)[0]
        self.p += 16
        return v

    def u32(self):
        self.align()
        v = struct.unpack_from('<I', self.d, self.p >> 3)[0]
        self.p += 32
        return v

    def f16(self):  # 8.8 fixed
        self.align()
        v = struct.unpack_from('<h', self.d, self.p >> 3)[0]
        self.p += 16
        return v / 256.0

    def cstr(self):
        self.align()
        s = self.p >> 3
        e = self.d.index(0, s)
        self.p = (e + 1) * 8
        return self.d[s:e].decode('latin-1')

    def rect(self):
        self.align()
        n = self.ub(5)
        r = [self.sb(n) for _ in range(4)]
        self.align()
        return r

    def matrix(self):
        """Returns (a, b, c, d, tx, ty) where x' = a*x + c*y + tx, y' = b*x + d*y + ty."""
        self.align()
        a = d = 1.0
        b = c = 0.0
        if self.ub(1):
            n = self.ub(5)
            a, d = self.fb(n), self.fb(n)
        if self.ub(1):
            n = self.ub(5)
            b, c = self.fb(n), self.fb(n)
        n = self.ub(5)
        tx, ty = self.sb(n), self.sb(n)
        self.align()
        return (a, b, c, d, tx, ty)

    def rgb(self):
        return (self.u8(), self.u8(), self.u8(), 255)

    def rgba(self):
        return (self.u8(), self.u8(), self.u8(), self.u8())

    def cxform(self, alpha):
        """Returns (mult[4], add[4]) with mult as floats."""
        self.align()
        has_add, has_mult = self.ub(1), self.ub(1)
        n = self.ub(4)
        cnt = 4 if alpha else 3
        mult = [1.0, 1.0, 1.0, 1.0]
        add = [0, 0, 0, 0]
        if has_mult:
            mult[:cnt] = [self.sb(n) / 256.0 for _ in range(cnt)]
        if has_add:
            add[:cnt] = [self.sb(n) for _ in range(cnt)]
        self.align()
        return (mult, add)


IDENTITY_CX = ([1.0, 1.0, 1.0, 1.0], [0, 0, 0, 0])


def read_tags(data, pos, end):
    # Some decompiled files are cut short at the very end; stop cleanly.
    end = min(end, len(data))
    while pos + 2 <= end:
        h = struct.unpack_from('<H', data, pos)[0]
        pos += 2
        code, ln = h >> 6, h & 0x3F
        if ln == 0x3F:
            ln = struct.unpack_from('<I', data, pos)[0]
            pos += 4
        if pos + ln > end:
            return
        yield code, pos, ln
        pos += ln
        if code == 0:
            return


# ---- shapes -----------------------------------------------------------------

def read_fill_styles(b, shape_ver):
    n = b.u8()
    if n == 0xFF and shape_ver >= 2:
        n = b.u16()
    out = []
    for _ in range(n):
        t = b.u8()
        if t == 0x00:
            out.append(('solid', b.rgba() if shape_ver >= 3 else b.rgb()))
        elif t in (0x10, 0x12, 0x13):
            m = b.matrix()
            b.align()
            spread_interp = b.u8()
            cnt = spread_interp & 0x0F
            stops = []
            for _ in range(cnt):
                r = b.u8()
                stops.append((r, b.rgba() if shape_ver >= 3 else b.rgb()))
            focal = b.f16() if t == 0x13 else 0.0
            out.append(('linear' if t == 0x10 else 'radial', m, stops, focal))
        elif t in (0x40, 0x41, 0x42, 0x43):
            bid = b.u16()
            m = b.matrix()
            out.append(('bitmap', bid, m, t in (0x40, 0x42)))
        else:
            raise ValueError('fill type %x' % t)
    return out


def read_line_styles(b, shape_ver):
    n = b.u8()
    if n == 0xFF:
        n = b.u16()
    out = []
    for _ in range(n):
        w = b.u16()
        if shape_ver == 4:
            b.ub(2); join = b.ub(2); has_fill = b.ub(1); b.ub(5); b.ub(8)
            if join == 2:
                b.u16()
            if has_fill:
                fs = read_fill_styles_one(b)
                color = fs[1] if fs[0] == 'solid' else (0, 0, 0, 255)
            else:
                color = b.rgba()
        else:
            color = b.rgba() if shape_ver >= 3 else b.rgb()
        out.append((w, color))
    return out


def read_fill_styles_one(b):
    # single FILLSTYLE wrapped in a 1-length array reader
    t = b.d[b.byte]
    tmp = bytearray([1]) + b.d[b.byte:]
    bb = Bits(bytes(tmp))
    r = read_fill_styles(bb, 4)[0]
    b.p += bb.p - 8
    return r


class Shape:
    """Edges grouped as paths per (style-table, fill index) / line index.

    fills: list of (style, [loops]) where each loop is a list of points
    (curves already flattened), in twips.
    lines: list of (width, color, [polylines]).
    """

    def __init__(self, cid, bounds, fills, lines):
        self.id = cid
        self.bounds = bounds
        self.fills = fills
        self.lines = lines


def _flatten_quad(p0, c, p1, out):
    # adaptive enough for prototype: segment count by control-point distance
    dx = abs(p0[0] - 2 * c[0] + p1[0]) + abs(p0[1] - 2 * c[1] + p1[1])
    n = max(2, min(16, int((dx / 20.0) ** 0.5 * 2) + 2))
    for i in range(1, n + 1):
        t = i / n
        mt = 1 - t
        out.append((mt * mt * p0[0] + 2 * mt * t * c[0] + t * t * p1[0],
                    mt * mt * p0[1] + 2 * mt * t * c[1] + t * t * p1[1]))


def _chain(edges):
    """edges: list of point-lists (each from start to end). Join into closed loops."""
    by_start = {}
    for i, e in enumerate(edges):
        by_start.setdefault(_key(e[0]), []).append(i)
    used = [False] * len(edges)
    loops = []
    for i in range(len(edges)):
        if used[i]:
            continue
        used[i] = True
        path = list(edges[i])
        start = _key(path[0])
        while _key(path[-1]) != start:
            nxt = None
            for j in by_start.get(_key(path[-1]), ()):
                if not used[j]:
                    nxt = j
                    break
            if nxt is None:
                break
            used[nxt] = True
            path.extend(edges[nxt][1:])
        loops.append(path)
    return loops


def _key(p):
    return (round(p[0]), round(p[1]))


def parse_shape(data, pos, ln, code):
    ver = {2: 1, 22: 2, 32: 3, 83: 4}[code]
    b = Bits(data, pos)
    cid = b.u16()
    bounds = b.rect()
    if ver == 4:
        b.rect()
        b.u8()
    fills = read_fill_styles(b, ver)
    lines = read_line_styles(b, ver)
    b.align()
    nf, nl = b.ub(4), b.ub(4)

    out_fills = []   # (style, edges)
    out_lines = []
    # current style tables -> edge buckets
    fill_edges = {}
    line_edges = {}

    def flush():
        for idx, edges in fill_edges.items():
            if edges:
                out_fills.append((fills[idx - 1], _chain(edges)))
        for idx, polys in line_edges.items():
            if polys:
                w, col = lines[idx - 1]
                out_lines.append((w, col, polys))
        fill_edges.clear()
        line_edges.clear()

    x = y = 0
    f0 = f1 = ln_ = 0
    while True:
        if b.ub(1) == 0:
            flags = b.ub(5)
            if flags == 0:
                break
            if flags & 1:
                n = b.ub(5)
                x, y = b.sb(n), b.sb(n)
            if flags & 2:
                f0 = b.ub(nf)
            if flags & 4:
                f1 = b.ub(nf)
            if flags & 8:
                ln_ = b.ub(nl)
            if flags & 16:
                flush()
                fills = read_fill_styles(b, ver)
                lines = read_line_styles(b, ver)
                b.align()
                nf, nl = b.ub(4), b.ub(4)
                f0 = f1 = ln_ = 0
        else:
            p0 = (x, y)
            seg = [p0]
            if b.ub(1):  # straight
                n = b.ub(4) + 2
                if b.ub(1):
                    x += b.sb(n)
                    y += b.sb(n)
                elif b.ub(1):
                    y += b.sb(n)
                else:
                    x += b.sb(n)
                seg.append((x, y))
            else:
                n = b.ub(4) + 2
                cx, cy = x + b.sb(n), y + b.sb(n)
                x, y = cx + b.sb(n), cy + b.sb(n)
                _flatten_quad(p0, (cx, cy), (x, y), seg)
            if f1:
                fill_edges.setdefault(f1, []).append(seg)
            if f0:
                fill_edges.setdefault(f0, []).append(seg[::-1])
            if ln_:
                line_edges.setdefault(ln_, []).append(seg)
    flush()
    return Shape(cid, bounds, out_fills, out_lines)


# ---- bitmaps ----------------------------------------------------------------

def parse_lossless(data, pos, ln, code):
    """Returns (id, w, h, premultiplied BGRA bytes for cairo ARGB32)."""
    b = Bits(data, pos)
    cid, fmt, w, h = b.u16(), b.u8(), b.u16(), b.u16()
    alpha = code == 36
    if fmt == 3:
        ncol = b.u8() + 1
        raw = zlib.decompress(data[b.byte:pos + ln])
        cs = 4 if alpha else 3
        pal = raw[:ncol * cs]
        stride = (w + 3) & ~3
        px = raw[ncol * cs:]
        out = bytearray(w * h * 4)
        for yy in range(h):
            for xx in range(w):
                i = px[yy * stride + xx]
                r, g, bl = pal[i * cs:i * cs + 3]
                a = pal[i * cs + 3] if alpha else 255
                o = (yy * w + xx) * 4
                out[o:o + 4] = bytes((bl, g, r, a))  # palette is premultiplied already
        return cid, w, h, bytes(out)
    if fmt == 5:
        raw = zlib.decompress(data[b.byte:pos + ln])
        out = bytearray(w * h * 4)
        for i in range(w * h):
            a, r, g, bl = raw[i * 4:i * 4 + 4]
            if not alpha:
                a = 255
            out[i * 4:i * 4 + 4] = bytes((bl, g, r, a))
        return cid, w, h, bytes(out)
    return cid, w, h, None


# ---- sprites / timeline -----------------------------------------------------

class Place:
    __slots__ = ('cid', 'matrix', 'cx', 'ratio', 'name', 'clip')

    def __init__(self):
        self.cid = None
        self.matrix = (1.0, 0.0, 0.0, 1.0, 0, 0)
        self.cx = IDENTITY_CX
        self.ratio = 0
        self.name = None
        self.clip = 0

    def copy(self):
        p = Place()
        p.cid, p.matrix, p.cx, p.ratio, p.name, p.clip = (
            self.cid, self.matrix, self.cx, self.ratio, self.name, self.clip)
        return p


class Sprite:
    """frames: list of dict depth -> Place (fully resolved display list per frame).
    stops: set of frame indices whose frame script contains stop()."""

    def __init__(self, cid, frames, labels, stops):
        self.id = cid
        self.frames = frames
        self.labels = labels
        self.stops = stops

    def frame_at(self, tick):
        """Playhead after `tick` frames of free play from frame 0, honouring stop()."""
        n = len(self.frames)
        if not self.stops:
            return tick % n
        first = min(self.stops)
        return min(tick, first)


def parse_place2(data, pos, ln, code, display):
    b = Bits(data, pos)
    flags = b.u8()
    if code == 70:
        flags2 = b.u8()
    depth = b.u16()
    if code == 70 and (flags2 & 0x08 or (flags2 & 0x10 and flags & 0x02)):
        b.cstr()  # class name
    move = flags & 1
    p = display[depth].copy() if (move and depth in display) else Place()
    if flags & 0x02:
        new_cid = b.u16()
        if move and p.cid != new_cid:
            p = Place()  # replacing character resets transform unless given
            p.matrix = display[depth].matrix
        p.cid = new_cid
    if flags & 0x04:
        p.matrix = b.matrix()
    if flags & 0x08:
        p.cx = b.cxform(True)
    if flags & 0x10:
        p.ratio = b.u16()
    if flags & 0x20:
        p.name = b.cstr()
    if flags & 0x40:
        p.clip = b.u16()
    display[depth] = p


def has_stop(data, pos, ln):
    """Walk an AS2 action stream looking for ActionStop (0x07)."""
    end = pos + ln
    while pos < end:
        op = data[pos]
        pos += 1
        if op == 0:
            break
        if op == 0x07:
            return True
        if op >= 0x80:
            pos += 2 + struct.unpack_from('<H', data, pos)[0]
    return False


def parse_timeline(data, pos, end):
    frames, labels, stops = [], {}, set()
    display = {}
    for code, p, ln in read_tags(data, pos, end):
        if code in (26, 70):
            parse_place2(data, p, ln, code, display)
        elif code == 28:
            display.pop(struct.unpack_from('<H', data, p)[0], None)
        elif code == 5:
            display.pop(struct.unpack_from('<H', data, p + 2)[0], None)
        elif code == 43:
            labels[Bits(data, p).cstr()] = len(frames)
        elif code == 12 and has_stop(data, p, ln):
            stops.add(len(frames))
        elif code == 1:
            frames.append(dict(display))
    return frames, labels, stops


class Movie:
    def __init__(self, path):
        data = open(path, 'rb').read()
        if data[:3] == b'CWS':
            data = data[:8] + zlib.decompress(data[8:])
        assert data[:3] == b'FWS', 'not an SWF'
        self.version = data[3]
        b = Bits(data, 8)
        self.stage = b.rect()
        self.fps = b.f16()
        self.frame_count = b.u16()
        start = b.byte
        self.shapes = {}
        self.sprites = {}
        self.bitmaps = {}
        self.exports = {}
        self.bgcolor = (255, 255, 255, 255)
        self.unknown_tags = {}
        for code, p, ln in read_tags(data, start, len(data)):
            if code in (2, 22, 32, 83):
                s = parse_shape(data, p, ln, code)
                self.shapes[s.id] = s
            elif code == 39:
                cid = struct.unpack_from('<H', data, p)[0]
                self.sprites[cid] = Sprite(cid, *parse_timeline(data, p + 4, p + ln))
            elif code in (20, 36):
                cid, w, h, px = parse_lossless(data, p, ln, code)
                self.bitmaps[cid] = (w, h, px)
            elif code == 56:
                bb = Bits(data, p)
                for _ in range(bb.u16()):
                    cid = bb.u16()
                    self.exports[bb.cstr()] = cid
            elif code == 9:
                self.bgcolor = Bits(data, p).rgb()
            elif code not in (0, 1, 26, 28, 43, 12, 59, 37, 48, 11, 10, 13, 62, 14, 34, 7,
                              45, 18, 19, 69, 75, 73, 88, 74, 65, 77, 24, 6, 21, 35, 8, 46):
                self.unknown_tags[code] = self.unknown_tags.get(code, 0) + 1
        self.root_frames, self.root_labels, _ = parse_timeline(data, start, len(data))
