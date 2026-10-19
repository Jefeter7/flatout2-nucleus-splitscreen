"""Build fo2pad as a 32-bit dinput8.dll.

usage: python build.py <path-to-compiler> [output.dll]

The compiler is a 32-bit MinGW gcc (e.g. w64devkit x86, used for the releases) or Tiny C Compiler 0.9.27.
The game looks up the undecorated name "DirectInput8Create": gcc exports it with --kill-at; tcc always
exports "_DirectInput8Create@20", so with tcc the name is rewritten in the finished DLL.
"""
import os
import struct
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
compiler = sys.argv[1]
out = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "dinput8.dll"))
src = os.path.join(here, "fo2pad.c")
is_tcc = os.path.basename(compiler).lower().startswith("tcc")

if is_tcc:
    cmd = [compiler, "-shared", "-O2", "-Wall", "-o", out, src, "-luser32", "-lkernel32"]
else:
    # a fixed image base keeps the output identical wherever it is built (by default ld derives it from the path)
    cmd = [compiler, "-shared", "-O2", "-s", "-Wall", "-o", out, src,
           "-Wl,--kill-at,--disable-auto-image-base,--image-base,0x63E00000", "-static-libgcc", "-luser32", "-lkernel32"]
subprocess.check_call(cmd)
leftover = os.path.splitext(out)[0] + ".def"
if os.path.exists(leftover):
    os.remove(leftover)

d = bytearray(open(out, "rb").read())
pe = struct.unpack_from("<I", d, 0x3C)[0]
if struct.unpack_from("<H", d, pe + 4)[0] != 0x14C:
    raise SystemExit("not a 32-bit DLL - use a 32-bit (i686) compiler")
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
    if not is_tcc or not old.startswith(b"_" + new):
        raise SystemExit("unexpected export name %r" % old)
    d[name_off:name_off + len(old)] = new + b"\0" * (len(old) - len(new))
    open(out, "wb").write(d)
print("built %s (%d bytes) with %s, export %s" % (out, len(d), os.path.basename(compiler), new.decode()))
