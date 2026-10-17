"""Build fo2pad as a 32-bit dinput8.dll with Tiny C Compiler 0.9.27.

usage: python build.py <path-to-tcc.exe> [output.dll]

TCC exports __stdcall functions with decorated names ("_DirectInput8Create@20"), but games look up the
plain name, so the single export's name is rewritten in place afterwards.
"""
import os, struct, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
tcc = sys.argv[1]
out = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "dinput8.dll"))

subprocess.check_call([tcc, "-shared", "-O2", "-Wall", "-o", out, os.path.join(here, "fo2pad.c"), "-luser32", "-lkernel32"])
leftover = os.path.splitext(out)[0] + ".def"
if os.path.exists(leftover):
    os.remove(leftover)

d = bytearray(open(out, "rb").read())
pe = struct.unpack_from("<I", d, 0x3C)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
opt = pe + 24
exp_rva = struct.unpack_from("<I", d, opt + 96)[0]
secs = []
so = opt + optsz
for _ in range(nsec):
    vs, va, rs, ro = struct.unpack_from("<IIII", d, so + 8)
    secs.append((va, vs, ro, rs))
    so += 40


def rva2off(rva):
    for va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs):
            return rva - va + ro
    raise ValueError("bad rva %x" % rva)


exp = rva2off(exp_rva)
count = struct.unpack_from("<I", d, exp + 24)[0]
names = rva2off(struct.unpack_from("<I", d, exp + 32)[0])
if count != 1:
    raise SystemExit("expected exactly one export, found %d" % count)
name_off = rva2off(struct.unpack_from("<I", d, names)[0])
old = bytes(d[name_off:d.index(0, name_off)])
new = b"DirectInput8Create"
if old != new:
    if not old.startswith(b"_" + new) or len(new) > len(old):
        raise SystemExit("unexpected export name %r" % old)
    d[name_off:name_off + len(old)] = new + b"\0" * (len(old) - len(new))
open(out, "wb").write(d)
print("built %s (%d bytes), export %s -> %s" % (out, len(d), old.decode(), new.decode()))
