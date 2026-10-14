# exestr.py FILE WORD...: offsets of each word as ASCII and UTF-16LE, with context
import sys, re
d = open(sys.argv[1], 'rb').read()
for w in sys.argv[2:]:
    for enc in ('latin1', 'utf-16-le'):
        pat = w.encode(enc)
        hits = [m.start() for m in re.finditer(re.escape(pat), d)][:6]
        print('%s (%s): %d hits %s' % (w, enc, len(hits), hits))
        for h in hits[:3]:
            ctx = d[max(0, h - 120):h + 160]
            print('   ', ctx.decode(enc, 'replace').replace('\0', '|').encode('ascii', 'replace').decode())
