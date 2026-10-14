# u16dump.py EXE START END [MAX]: the NUL-terminated UTF-16LE strings in a file range, in order
import sys
d = open(sys.argv[1], 'rb').read()
s, e = int(sys.argv[2], 0), int(sys.argv[3], 0)
mx = int(sys.argv[4]) if len(sys.argv) > 4 else 400
p = s
if p & 1: p += 1
out = 0
while p < e and out < mx:
    q = p
    while q + 1 < e and (d[q] or d[q + 1]): q += 2
    if q > p:
        t = d[p:q].decode('utf-16-le', 'replace')
        print('%08x %s' % (p, t.encode('unicode_escape').decode()[:110]))
        out += 1
    p = q + 2
