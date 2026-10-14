# Top-level (non-function) code of the root timeline's DoActions, per frame.
import sys, struct
src = open(sys.argv[1]).read()
exec(src.split("path, kw = sys.argv[2], sys.argv[3]")[0].replace("exec(open(sys.argv[1]).read().split(\"for path in\")[0])",
     "exec(open(sys.argv[2]).read().split('for path in')[0])"))
d = open(sys.argv[3], 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
frame = 0
for code, p, l in tags(d, start, len(d)):
    if code == 1: frame += 1
    elif code == 43: print('--- label', cstr(d, p)[0], 'at frame', frame)
    elif code == 12:
        print('=== frame', frame, 'DoAction', l, 'bytes')
        depth = 0
        for ln in dis(d, p, p + l, []):
            s = ln.strip()
            if s.startswith('function'):
                if depth == 0: print('   ', s)
                depth += 1
                continue
            if s == '}':
                depth -= 1
                continue
            if depth == 0: print('   ', s)
