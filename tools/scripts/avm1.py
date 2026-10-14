# Minimal AVM1 disassembler: finds the action block containing a keyword and prints
# the function(s) that use it.  usage: avm1.py probe.py file.swf keyword
import sys, struct
exec(open(sys.argv[1]).read().split("for path in")[0])
OPS = {0x04:'NextFrame',0x05:'PrevFrame',0x06:'Play',0x07:'Stop',0x0A:'Add',0x0B:'Subtract',0x0C:'Multiply',
 0x0D:'Divide',0x0E:'Equals',0x0F:'Less',0x10:'And',0x11:'Or',0x12:'Not',0x13:'StringEquals',0x17:'Pop',
 0x18:'ToInteger',0x1C:'GetVariable',0x1D:'SetVariable',0x20:'SetTarget2',0x21:'StringAdd',0x22:'GetProperty',
 0x23:'SetProperty',0x24:'CloneSprite',0x25:'RemoveSprite',0x26:'Trace',0x34:'GetTime',0x3A:'Delete',
 0x3B:'Delete2',0x3C:'DefineLocal',0x3D:'CallFunction',0x3E:'Return',0x3F:'Modulo',0x40:'NewObject',
 0x41:'DefineLocal2',0x42:'InitArray',0x43:'InitObject',0x44:'TypeOf',0x45:'TargetPath',0x46:'Enumerate',
 0x47:'Add2',0x48:'Less2',0x49:'Equals2',0x4A:'ToNumber',0x4B:'ToString',0x4C:'PushDuplicate',0x4D:'StackSwap',
 0x4E:'GetMember',0x4F:'SetMember',0x50:'Increment',0x51:'Decrement',0x52:'CallMethod',0x53:'NewMethod',
 0x60:'BitAnd',0x61:'BitOr',0x62:'BitXor',0x63:'BitLShift',0x64:'BitRShift',0x65:'BitURShift',
 0x66:'StrictEquals',0x67:'Greater',0x68:'StringGreater',0x81:'GotoFrame',0x83:'GetURL',0x87:'StoreRegister',
 0x88:'ConstantPool',0x8B:'SetTarget',0x8C:'GotoLabel',0x8E:'DefineFunction2',0x94:'With',0x96:'Push',
 0x99:'Jump',0x9A:'GetURL2',0x9B:'DefineFunction',0x9D:'If',0x9F:'GotoFrame2'}

def dis(d, p, end, pool, indent=''):
    out = []
    while p < end:
        op = d[p]; start = p; p += 1
        if op == 0: out.append(indent + 'End'); break
        l = 0
        if op >= 0x80: l = struct.unpack_from('<H', d, p)[0]; p += 2
        body = d[p:p + l]; arg = ''
        if op == 0x88:
            n = struct.unpack_from('<H', body, 0)[0]; pool[:] = body[2:].split(b'\0')[:n]
            pool[:] = [s.decode('latin1') for s in pool]; arg = '%d strings' % n
        elif op in (0x96, 0xa0, 0xa1, 0xa2, 0xa3):   # a0-a3: the game's own Push variants
            q = 0; vals = []
            while q < l:
                t = body[q]; q += 1
                if t == 0: e = body.index(b'\0', q); vals.append(repr(body[q:e].decode('latin1'))); q = e + 1
                elif t == 1: vals.append('%g' % struct.unpack_from('<f', body, q)[0]); q += 4
                elif t == 2: vals.append('null')
                elif t == 3: vals.append('undef')
                elif t == 4: vals.append('r%d' % body[q]); q += 1
                elif t == 5: vals.append('true' if body[q] else 'false'); q += 1
                elif t == 6:
                    b8 = body[q + 4:q + 8] + body[q:q + 4]; vals.append('%g' % struct.unpack('<d', b8)[0]); q += 8
                elif t == 7: vals.append('%d' % struct.unpack_from('<i', body, q)[0]); q += 4
                elif t == 8: vals.append('"%s"' % pool[body[q]]); q += 1
                elif t == 9: vals.append('"%s"' % pool[struct.unpack_from('<H', body, q)[0]]); q += 2
                else: vals.append('?'); break
            arg = ', '.join(vals)
        elif op in (0x9B, 0x8E):
            e = body.index(b'\0'); name = body[:e].decode('latin1'); q = e + 1
            n = struct.unpack_from('<H', body, q)[0]; q += 2
            if op == 0x8E: q += 3
            params = []
            for _ in range(n):
                if op == 0x8E: q += 1
                e = body.index(b'\0', q); params.append(body[q:e].decode('latin1')); q = e + 1
            size = struct.unpack_from('<H', body, q)[0]
            out.append(indent + 'function %s(%s) {' % (name, ', '.join(params)))
            out += dis(d, p + l, p + l + size, pool, indent + '  ')
            out.append(indent + '}')
            p += l + size; continue
        elif op in (0x99, 0x9D): arg = '%+d -> @%d' % (struct.unpack_from('<h', body, 0)[0], p + l + struct.unpack_from('<h', body, 0)[0])
        elif op == 0x87: arg = 'r%d' % body[0]
        elif op >= 0xa0 and op not in OPS:
            arg = 'raw ' + body.hex()
            idx = [struct.unpack_from('<H', body, i)[0] for i in range(0, len(body) - 1)]
            arg += '  pool?[' + ', '.join(pool[i] for i in idx[:1] if i < len(pool)) + ']'
        out.append('%s@%d %s %s' % (indent, start, OPS.get(op, hex(op)), arg))
        p += l
    return out

path, kw = sys.argv[2], sys.argv[3]
d = open(path, 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
def blocks(p0, e0):
    for code, p, l in tags(d, p0, e0):
        if code == 12: yield p, p + l
        elif code == 59: yield p + 2, p + l
        elif code == 39: yield from blocks(p + 4, p + l)
for p, e in blocks(start, len(d)):
    if kw.encode() in d[p:e]:
        lines = dis(d, p, e, [])
        print('# block', p, e, len(lines), 'lines;', sum(kw in x for x in lines), 'mention', file=sys.stderr)
        if len(sys.argv) > 4:
            print('\n'.join(lines)); continue
        # print only functions mentioning the keyword
        depth, buf, keep = 0, [], False
        for ln in lines:
            s = ln.strip()
            if s.startswith('function') and depth == 0: buf, keep = [], False
            if s.startswith('function'): depth += 1
            if depth: buf.append(ln)
            if kw in ln: keep = True
            if s == '}':
                depth -= 1
                if depth == 0 and keep: print('\n'.join(buf)); print()
