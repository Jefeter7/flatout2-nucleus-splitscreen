"""Package the handler as a Nucleus Co-op .nc archive.

usage: python tools/make_nc.py [output.nc]

Nucleus installs a .nc by renaming handler.js to "<Game.GameName>.js" and moving the contents of every
top-level folder of the archive into handlers\\<Game.GameName>\\. It only recognises a folder when the
archive has a separate entry for it, so those entries are written explicitly.
"""
import os
import sys
import zipfile

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
handlers = os.path.join(repo, "handlers")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, "FlatOut 2 (Nucleus handler).nc"))
skip_ext = (".log", ".def", ".nc")

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(os.path.join(handlers, "FlatOut 2.js"), "handler.js")
    folder = os.path.join(handlers, "FlatOut 2")
    for dirpath, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        rel = os.path.relpath(dirpath, handlers).replace("\\", "/")
        z.writestr(rel + "/", "")
        for name in sorted(files):
            if name.endswith(skip_ext) or name == "fo2pad_remote.txt":
                continue
            z.write(os.path.join(dirpath, name), rel + "/" + name)

with zipfile.ZipFile(out) as z:
    for entry in z.namelist():
        print("  " + entry)
print("wrote %s (%d bytes)" % (out, os.path.getsize(out)))
