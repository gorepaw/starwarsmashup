"""Decode Battlefront II's compiled mission scripts (Lua 5.0 bytecode).

  python -I tools/assets/bf2_lua.py <file.lvl> --list              every script (scr_) in the file
  python -I tools/assets/bf2_lua.py <mission.lvl> <sublevel>...    a mission's scripts (spa8g_ass, hot1g_con, ...)
  python -I tools/assets/bf2_lua.py <file.lvl> --script <name>...  a script by its own name (objectivespaceassault)

Prints the scripts' global calls and assignments with their constant
arguments, and tables built with NEWTABLE / SETTABLE / SETLIST with their
contents. script_events() gives the same as data (space_assault.py reads it).

How the files are laid out (worked out for this project; see the
bf2-mission-scripts notes in docs): mission.lvl holds a `lvl_` sub-level per
mission, named by a hash (FNV-1a over each byte | 0x20) of
`<map><c|g>_<con|ctf|1flag|hunt|xl|ass|eli>`, each holding `scr_` chunks
(NAME, BODY); shared scripts (objectivespaceassault, linkedturrets, ...) are
`scr_` chunks of their own. A BODY is a Lua 5.0 chunk: an 18-byte header, then
the main function. Numbers are 4-byte floats; RK operands index constants
from 128.
"""
import struct
import sys


def fnv(s):
    h = 0x811c9dc5
    for c in s.encode():
        h ^= (c | 0x20)
        h = (h * 0x01000193) & 0xffffffff
    return h


def chunks(data, off, end):
    while off + 8 <= end:
        tag = data[off:off + 4].decode('latin1')
        size = struct.unpack_from('<I', data, off + 4)[0]
        yield tag, off + 8, size
        off += 8 + ((size + 3) & ~3)


class R:
    def __init__(s, d, o):
        s.d = d
        s.o = o

    def b(s):
        v = s.d[s.o]
        s.o += 1
        return v

    def i(s):
        v = struct.unpack_from('<i', s.d, s.o)[0]
        s.o += 4
        return v

    def u(s):
        v = struct.unpack_from('<I', s.d, s.o)[0]
        s.o += 4
        return v

    def f(s):
        v = struct.unpack_from('<f', s.d, s.o)[0]
        s.o += 4
        return v

    def st(s):
        n = s.u()
        if n == 0:
            return None
        v = s.d[s.o:s.o + n - 1].decode('latin1')
        s.o += n
        return v


def proto(r):
    r.st()
    r.i()
    nups = r.b()
    r.b()
    r.b()
    r.b()
    for _ in range(r.i()):
        r.i()
    for _ in range(r.i()):
        r.st()
        r.i()
        r.i()
    for _ in range(r.i()):
        r.st()
    consts = []
    for _ in range(r.i()):
        t = r.b()
        consts.append(r.f() if t == 3 else r.st() if t == 4 else None)
    protos = [proto(r) for _ in range(r.i())]
    code = [r.u() for _ in range(r.i())]
    return consts, protos, code, nups


class Table:
    def __init__(self):
        self.items = []  # (key, value), in order

    def set(self, k, v):
        for n, (kk, _) in enumerate(self.items):
            if kk == k:
                self.items[n] = (k, v)
                return
        self.items.append((k, v))

    def __str__(self):
        parts = []
        for k, v in self.items:
            if isinstance(k, float) and k == int(k):
                parts.append(show(v))
            else:
                parts.append('%s = %s' % (k if isinstance(k, str) else '[%s]' % show(k), show(v)))
        return '{' + ', '.join(parts) + '}'


def show(v):
    if isinstance(v, Table):
        return str(v)
    if isinstance(v, float):
        return '%g' % v
    if isinstance(v, str) and v.startswith('\x00'):
        return v[1:]  # a symbolic value (a global, a call's result)
    return repr(v)


def sym(s):
    return '\x00' + s


FPF = 32
EVENTS = []  # ('call', function, [args]) and ('global', name, value), as decoded


def calls(p, out, ups=(), depth=0):
    consts, protos, code, _n = p
    reg = {}
    pending = []

    def rk(x):
        return consts[x - 128] if x >= 128 else reg.get(x, sym('?'))

    skip = 0
    cl = None
    for ins in code:
        op = ins & 63
        c = (ins >> 6) & 511
        b = (ins >> 15) & 511
        a = ins >> 24
        bx = (ins >> 6) & 0x3ffff
        if skip:
            cl[1].append(reg.get(b, sym('?')) if op == 0 else (ups[b] if b < len(ups) else sym('?')))
            skip -= 1
            if not skip:
                pending.append(cl)
            continue
        if op == 34:  # CLOSURE
            q = protos[bx]
            cl = (q, [])
            if q[3] == 0:
                pending.append(cl)
            skip = q[3]
            reg[a] = sym('closure')
        elif op == 4:  # GETUPVAL
            reg[a] = ups[b] if b < len(ups) else sym('?')
        elif op == 0:
            reg[a] = reg.get(b, sym('?'))
        elif op == 1:
            reg[a] = consts[bx]
        elif op == 2:
            reg[a] = sym('true' if b else 'false')
        elif op == 3:
            for k in range(a, b + 1):
                reg[k] = sym('nil')
        elif op == 5:
            reg[a] = sym('@' + str(consts[bx]))
        elif op == 6:
            t = reg.get(b, sym('?'))
            k = rk(c)
            got = None
            if isinstance(t, Table):
                for kk, vv in t.items:
                    if kk == k:
                        got = vv
            reg[a] = got if got is not None else sym('%s[%s]' % (show(t), show(k)))
        elif op == 7:
            EVENTS.append(('global', consts[bx], reg.get(a, sym('?'))))
            out.append('%s = %s' % (consts[bx], show(reg.get(a, sym('?')))))
        elif op == 9:  # SETTABLE
            t = reg.get(a)
            if isinstance(t, Table):
                t.set(rk(b), rk(c))
            else:
                out.append('%s[%s] = %s' % (show(t or sym('?')), show(rk(b)), show(rk(c))))
        elif op == 10:
            reg[a] = Table()
        elif op == 11:  # SELF
            reg[a + 1] = reg.get(b, sym('?'))
            reg[a] = sym('%s:%s' % (show(reg.get(b, sym('?'))), rk(c)))
        elif op in (31, 32):  # SETLIST, SETLISTO
            t = reg.get(a)
            n = bx % FPF + 1
            base = bx - bx % FPF
            if isinstance(t, Table):
                for i in range(1, n + 1):
                    t.set(float(base + i), reg.get(a + i, sym('?')))
        elif op == 25:  # CALL
            n = (b - 1) if b else 0
            EVENTS.append(('call', show(reg.get(a, sym('?'))), [reg.get(a + 1 + k, sym('?')) for k in range(n)]))
            args = ', '.join(show(reg.get(a + 1 + k, sym('?'))) for k in range(n))
            out.append('%s%s(%s)' % ('  ' * depth, show(reg.get(a, sym('?'))), args))
            reg[a] = sym('ret')
        elif op == 26:
            out.append('tailcall %s' % show(reg.get(a, sym('?'))))
        elif op in (12, 13, 14, 15, 19):
            reg[a] = sym('expr')
    for q, u in pending:
        calls(q, out, u, depth)


def dump_body(data, o2):
    r = R(data, o2 + 18)
    out = []
    calls(proto(r), out)
    print('\n'.join(out))


def scripts(data, off, end):
    # (name, body offset) of every scr_ under [off, end).
    for t, so, sz in chunks(data, off, end):
        if t == 'scr_':
            name, body = None, None
            for t2, o2, s2 in chunks(data, so, so + sz):
                if t2 == 'NAME':
                    name = data[o2:o2 + s2].split(b'\0')[0].decode('latin1')
                elif t2 == 'BODY':
                    body = o2
            yield name, body
        elif t in ('lvl_', 'ucfb'):
            start = so + 8 if t == 'lvl_' else so
            yield from scripts(data, start, so + sz)


def script_events(path, sublevel=None, script=None):
    # The decoded events of a mission sub-level's scripts, or one shared script.
    data = open(path, 'rb').read()
    EVENTS.clear()
    import io
    import contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        if script:
            for name, body in scripts(data, 8, len(data)):
                if name and name.lower() == script.lower() and body is not None:
                    dump_body(data, body)
        else:
            want = fnv(sublevel)
            for tag, off, size in chunks(data, 8, len(data)):
                if tag == 'lvl_' and struct.unpack_from('<I', data, off)[0] == want:
                    for name, body in scripts(data, off + 8, off + size):
                        if body is not None:
                            dump_body(data, body)
    return list(EVENTS)


def main():
    data = open(sys.argv[1], 'rb').read()
    args = sys.argv[2:]
    if args and args[0] == '--list':
        for name, body in scripts(data, 8, len(data)):
            print(name)
    elif args and args[0] == '--script':
        want = set(a.lower() for a in args[1:])
        for name, body in scripts(data, 8, len(data)):
            if name and name.lower() in want and body is not None:
                print('=====', name)
                dump_body(data, body)
    else:
        wanted = {fnv(n): n for n in args}
        for tag, off, size in chunks(data, 8, len(data)):
            if tag != 'lvl_':
                continue
            h = struct.unpack_from('<I', data, off)[0]
            if h not in wanted:
                continue
            print('=====', wanted[h])
            for name, body in scripts(data, off + 8, off + size):
                if body is not None:
                    dump_body(data, body)


if __name__ == '__main__':
    main()
