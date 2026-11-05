import os
import sys
sys.path.insert(0, sys.argv[0].rsplit('/', 1)[0])
pairs = sys.argv[2:]; sys.argv = sys.argv[:2]
import fit
import numpy as np
for p in pairs:
    bi, cid = map(int, p.split(':'))
    r = fit.fit(bi, cid, 0, list(np.arange(1.9, 2.61, 0.05)), False)
    print(bi, cid, 'score %.2f k %.2f origin %.1f %.1f' % (r[0], r[1], r[2], r[3]), flush=True)
