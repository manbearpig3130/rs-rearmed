#!/usr/bin/env python3
"""OpenGL's headers in the form shims/opengl/tools/gen-gl-api.py reads, made from Khronos's as Mesa installs them.

    python3 linux/gl/khronos_headers.py OUTDIR [INCLUDE_DIR]     (linux/Makefile runs it)

The generator of libRSOpenGL's wrapper table was written for the Mac SDK's OpenGL.framework headers: it reads
seven files by name and takes a prototype from one line. Khronos's gl.h spreads some prototypes over several
lines and writes GLAPIENTRY where glext.h writes APIENTRY. This writes OUTDIR/gl.h and glext.h with each prototype
on one line, the CGL files from linux/compat/OpenGL (the CGL calls this port answers over EGL), and the files the
Mac has and Linux has not, empty: so the generator, and the enum table's, run unchanged.
"""
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COMPAT = os.path.join(HERE, "..", "compat", "OpenGL")


def one_line_prototypes(text):
    """Each `GLAPI ... name(...);` joined onto one line, GLAPIENTRY written as APIENTRY."""
    text = text.replace("GLAPIENTRY", "APIENTRY")

    def join(m):
        return " ".join(m.group(0).split())

    return re.sub(r"^[ \t]*GLAPI\b[^;{}#]*?\([^;{}#]*?\)\s*;", join, text, flags=re.M | re.S)


def main(argv):
    if len(argv) not in (2, 3):
        sys.stderr.write(__doc__)
        return 2
    out, inc = argv[1], argv[2] if len(argv) == 3 else "/usr/include"
    os.makedirs(out, exist_ok=True)
    for name in ("gl.h", "glext.h"):
        src = os.path.join(inc, "GL", name)
        try:
            with open(src, errors="replace") as f:
                text = f.read()
        except OSError as e:
            sys.stderr.write("khronos_headers: %s: %s (the OpenGL development headers are needed: mesa-libGL-devel)\n"
                             % (src, e.strerror))
            return 1
        with open(os.path.join(out, name), "w") as f:
            f.write(one_line_prototypes(text))
    for name in ("CGLCurrent.h", "OpenGL.h", "CGLDevice.h"):
        shutil.copyfile(os.path.join(COMPAT, name), os.path.join(out, name))
    for name in ("gl3.h", "CGLContext.h"):
        open(os.path.join(out, name), "w").close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
