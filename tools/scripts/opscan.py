# Counts every AVM1 opcode used in all scripts (DoAction, DoInitAction, clip actions) of the given SWFs.
import sys, struct, glob, collections
exec(open(sys.argv[1]).read().split("for path in")[0])

ops = collections.Counter()
where = {}
def walk(d, p, end, fname):
    end = min(end, len(d))
    while p < end:
        op = d[p]; p += 1
        if op == 0: break
        l = 0
        if op >= 0x80:
            if p + 2 > end: break
            l = struct.unpack_from('<H', d, p)[0]; p += 2
        ops[op] += 1
        where.setdefault(op, fname)
        if op == 0xA4 and ops[op] <= 6:
            ctx = d[max(0, p - 40):p + l + 30]
            print('A4 in %s len %d body %s | around: %s' % (fname, l, d[p:p + l].hex(),
                  ''.join(chr(c) if 32 <= c < 127 else '.' for c in ctx)))
        p += l   # function bodies follow their DefineFunction header inline, so a linear walk covers them

def clip_actions(d, p, end, ver, fname):
    p += 2                      # reserved
    p += 4 if ver >= 6 else 2   # all event flags
    while p < end:
        flags = struct.unpack_from('<I' if ver >= 6 else '<H', d, p)[0]; p += 4 if ver >= 6 else 2
        if flags == 0: break
        size = struct.unpack_from('<I', d, p)[0]; p += 4
        body = p
        if flags & 0x00020000: body += 1   # key press: key code
        walk(d, body, p + size, fname)
        p += size

def skip_place2_to_actions(d, q, end):
    flags = d[q]; q += 3
    if flags & 2: q += 2
    for bit, kind in ((4, 'm'), (8, 'cx')):
        if flags & bit:
            bits = ''.join(format(b, '08b') for b in d[q:q + 200]); i = 0
            if kind == 'm':
                for _ in range(2):
                    if bits[i] == '1': n = int(bits[i+1:i+6], 2); i += 6 + 2 * n
                    else: i += 1
                n = int(bits[i:i+5], 2); i += 5 + 2 * n
            else:
                ha, hm = bits[0] == '1', bits[1] == '1'; n = int(bits[2:6], 2); i = 6
                if hm: i += 4 * n
                if ha: i += 4 * n
            q += (i + 7) // 8
    if flags & 0x10: q += 2
    if flags & 0x20:
        while d[q]: q += 1
        q += 1
    if flags & 0x40: q += 2
    return q if flags & 0x80 else None

def scan(d, p, end, ver, fname):
    for code, q, l in tags(d, p, end):
        if code == 12: walk(d, q, q + l, fname)
        elif code == 59: walk(d, q + 2, q + l, fname)
        elif code == 39: scan(d, q + 4, q + l, ver, fname)
        elif code == 26 and q < len(d) and d[q] & 0x80:
            try:
                a = skip_place2_to_actions(d, q, q + l)
            except (ValueError, IndexError):
                print('  (bad PlaceObject2 in %s at %d)' % (fname, q)); continue
            try:
                if a: clip_actions(d, a, q + l, ver, fname)
            except (struct.error, IndexError):
                print('  (bad clip actions in %s at %d)' % (fname, q))

for f in sys.argv[2:]:
    d = open(f, 'rb').read()
    nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8
    scan(d, 8 + rb + 4, len(d), d[3], f.split('/')[-1])

STD = {0x04:'NextFrame',0x05:'PrevFrame',0x06:'Play',0x07:'Stop',0x08:'ToggleQuality',0x09:'StopSounds',0x0A:'Add',0x0B:'Subtract',0x0C:'Multiply',0x0D:'Divide',0x0E:'Equals',0x0F:'Less',0x10:'And',0x11:'Or',0x12:'Not',0x13:'StringEquals',0x14:'StringLength',0x15:'StringExtract',0x17:'Pop',0x18:'ToInteger',0x1C:'GetVariable',0x1D:'SetVariable',0x20:'SetTarget2',0x21:'StringAdd',0x22:'GetProperty',0x23:'SetProperty',0x24:'CloneSprite',0x25:'RemoveSprite',0x26:'Trace',0x27:'StartDrag',0x28:'EndDrag',0x29:'StringLess',0x30:'RandomNumber',0x31:'MBStringLength',0x32:'CharToAscii',0x33:'AsciiToChar',0x34:'GetTime',0x35:'MBStringExtract',0x36:'MBCharToAscii',0x37:'MBAsciiToChar',0x3A:'Delete',0x3B:'Delete2',0x3C:'DefineLocal',0x3D:'CallFunction',0x3E:'Return',0x3F:'Modulo',0x40:'NewObject',0x41:'DefineLocal2',0x42:'InitArray',0x43:'InitObject',0x44:'TypeOf',0x45:'TargetPath',0x46:'Enumerate',0x47:'Add2',0x48:'Less2',0x49:'Equals2',0x4A:'ToNumber',0x4B:'ToString',0x4C:'PushDuplicate',0x4D:'StackSwap',0x4E:'GetMember',0x4F:'SetMember',0x50:'Increment',0x51:'Decrement',0x52:'CallMethod',0x53:'NewMethod',0x54:'InstanceOf',0x55:'Enumerate2',0x60:'BitAnd',0x61:'BitOr',0x62:'BitXor',0x63:'BitLShift',0x64:'BitRShift',0x65:'BitURShift',0x66:'StrictEquals',0x67:'Greater',0x68:'StringGreater',0x69:'Extends',0x81:'GotoFrame',0x83:'GetURL',0x87:'StoreRegister',0x88:'ConstantPool',0x8A:'WaitForFrame',0x8B:'SetTarget',0x8C:'GotoLabel',0x8D:'WaitForFrame2',0x8E:'DefineFunction2',0x8F:'Try',0x94:'With',0x96:'Push',0x99:'Jump',0x9A:'GetURL2',0x9B:'DefineFunction',0x9D:'If',0x9E:'Call',0x9F:'GotoFrame2'}
for op, n in sorted(ops.items()):
    print('0x%02X %-14s %7d   e.g. %s' % (op, STD.get(op, '** NON-STANDARD **'), n, where[op]))
