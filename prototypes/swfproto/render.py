"""Cairo renderer for swf.Movie, used to check the parser before porting to GXM.

usage: python3 render.py PLAYER.swf walk out_dir [--frames N] [--scale S]
Renders the root-timeline frame with that label, advancing nested sprites
one tick per output frame, and writes PNGs plus an animated GIF.
"""
import os
import sys

import cairo

import swf


def mul(m, n):
    """Concatenate: apply n first, then m."""
    a, b, c, d, tx, ty = m
    a2, b2, c2, d2, tx2, ty2 = n
    return (a * a2 + c * b2, b * a2 + d * b2,
            a * c2 + c * d2, b * c2 + d * d2,
            a * tx2 + c * ty2 + tx, b * tx2 + d * ty2 + ty)


def cx_mul(outer, inner):
    (om, oa), (im, ia) = outer, inner
    return ([om[i] * im[i] for i in range(4)],
            [oa[i] + om[i] * ia[i] for i in range(4)])


def apply_cx(col, cx):
    m, a = cx
    out = [max(0, min(255, col[i] * m[i] + a[i])) / 255.0 for i in range(4)]
    return out


def to_cairo(m):
    a, b, c, d, tx, ty = m
    return cairo.Matrix(a, b, c, d, tx, ty)


class Renderer:
    def __init__(self, movie):
        self.m = movie
        self.surfaces = {}

    def bitmap(self, bid):
        if bid not in self.surfaces:
            w, h, px = self.m.bitmaps.get(bid, (1, 1, None))
            if px is None:
                surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, 1, 1)
            else:
                stride = cairo.ImageSurface.format_stride_for_width(cairo.FORMAT_ARGB32, w)
                buf = bytearray(stride * h)
                for y in range(h):
                    buf[y * stride:y * stride + w * 4] = px[y * w * 4:(y + 1) * w * 4]
                surf = cairo.ImageSurface.create_for_data(buf, cairo.FORMAT_ARGB32, w, h, stride)
            self.surfaces[bid] = surf
        return self.surfaces[bid]

    def source_for(self, ctx, style, cx):
        kind = style[0]
        if kind == 'solid':
            ctx.set_source_rgba(*apply_cx(style[1], cx))
            return
        if kind in ('linear', 'radial'):
            _, gm, stops, focal = style
            if kind == 'linear':
                pat = cairo.LinearGradient(-16384, 0, 16384, 0)
            else:
                pat = cairo.RadialGradient(focal * 16384, 0, 0, 0, 0, 16384)
            for r, col in stops:
                pat.add_color_stop_rgba(r / 255.0, *apply_cx(col, cx))
            mtx = to_cairo(gm)
            mtx.invert()
            pat.set_matrix(mtx)
            ctx.set_source(pat)
            return
        if kind == 'bitmap':
            _, bid, bm, smooth = style
            pat = cairo.SurfacePattern(self.bitmap(bid))
            mtx = to_cairo(bm)
            try:
                mtx.invert()
            except cairo.Error:
                mtx = cairo.Matrix()
            pat.set_matrix(mtx)
            pat.set_filter(cairo.FILTER_GOOD if smooth else cairo.FILTER_NEAREST)
            ctx.set_source(pat)

    def draw_shape(self, ctx, shape, mtx, cx):
        ctx.save()
        ctx.set_matrix(to_cairo(mtx))
        for style, loops in shape.fills:
            ctx.new_path()
            for loop in loops:
                ctx.move_to(*loop[0])
                for p in loop[1:]:
                    ctx.line_to(*p)
                ctx.close_path()
            ctx.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
            self.source_for(ctx, style, cx)
            ctx.fill()
        for w, col, polys in shape.lines:
            ctx.new_path()
            for poly in polys:
                ctx.move_to(*poly[0])
                for p in poly[1:]:
                    ctx.line_to(*p)
            ctx.set_source_rgba(*apply_cx(col, cx))
            ctx.set_line_width(max(w, 20))  # hairline = 1px (20 twips)
            ctx.set_line_cap(cairo.LINE_CAP_ROUND)
            ctx.set_line_join(cairo.LINE_JOIN_ROUND)
            ctx.stroke()
        ctx.restore()

    def draw_display_list(self, ctx, display, mtx, cx, tick):
        depths = sorted(display)
        i = 0
        while i < len(depths):
            pl = display[depths[i]]
            if pl.clip:
                # this object masks everything above it up to pl.clip
                masked = {}
                j = i + 1
                while j < len(depths) and depths[j] <= pl.clip:
                    masked[depths[j]] = display[depths[j]]
                    j += 1
                ctx.push_group()
                self.draw_display_list(ctx, masked, mtx, cx, tick)
                content = ctx.pop_group()
                ctx.push_group()
                self.draw_char(ctx, pl.cid, mul(mtx, pl.matrix), swf.IDENTITY_CX, tick, pl.ratio)
                mask = ctx.pop_group()
                ctx.set_source(content)
                ctx.mask(mask)
                i = j
                continue
            self.draw_char(ctx, pl.cid, mul(mtx, pl.matrix), cx_mul(cx, pl.cx), tick, pl.ratio)
            i += 1

    def draw_char(self, ctx, cid, mtx, cx, tick, ratio=0):
        if cid in self.m.shapes:
            self.draw_shape(ctx, self.m.shapes[cid], mtx, cx)
        elif cid in self.m.sprites:
            sp = self.m.sprites[cid]
            if sp.frames:
                self.draw_display_list(ctx, sp.frames[sp.frame_at(tick)], mtx, cx, tick)

    def render_frame(self, frame_index, tick, w, h, scale, origin):
        surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
        ctx = cairo.Context(surf)
        ctx.set_source_rgba(0.93, 0.93, 0.88, 1)
        ctx.paint()
        root = (scale / 20.0, 0.0, 0.0, scale / 20.0, origin[0], origin[1])
        self.draw_display_list(ctx, self.m.root_frames[frame_index], root, swf.IDENTITY_CX, tick)
        return surf


def main():
    path, label, out = sys.argv[1], sys.argv[2], sys.argv[3]
    nframes = int(sys.argv[sys.argv.index('--frames') + 1]) if '--frames' in sys.argv else 30
    scale = float(sys.argv[sys.argv.index('--scale') + 1]) if '--scale' in sys.argv else 1.0
    os.makedirs(out, exist_ok=True)
    m = swf.Movie(path)
    fi = m.root_labels[label]
    r = Renderer(m)
    w, h = int(320 * scale), int(320 * scale)
    origin = (w / 2, h * 0.8)
    files = []
    for t in range(nframes):
        surf = r.render_frame(fi, t, w, h, scale, origin)
        fn = os.path.join(out, '%s_%03d.png' % (label, t))
        surf.write_to_png(fn)
        files.append(fn)
    print('wrote', len(files), 'frames to', out)


if __name__ == '__main__':
    main()
