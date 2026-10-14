# Find which SWF scripts mention the parallax layers and dump their constant pools.
import sys, glob, re, os
for f in glob.glob(sys.argv[1] + '/game/*.swf'):
    d = open(f, 'rb').read()
    for m in re.finditer(rb'f_UpdateCamera|bg3_c', d):
        print(os.path.basename(f), m.group().decode(), m.start())
        break
