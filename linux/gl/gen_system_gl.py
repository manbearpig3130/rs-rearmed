#!/usr/bin/env python3
"""A forwarder for every OpenGL function of libRSOpenGL's table, for libRSSystemGL.so (linux/gl/system_gl.c).

    python3 linux/gl/gen_system_gl.py GL_API.INC OUT.c     (linux/Makefile runs it)

libRSOpenGL asks the system's OpenGL for each function of its table by name. Apple's framework exports them all;
libglvnd's libOpenGL exports the core names and not every older alias (glDrawArraysInstancedARB). So each name is
a function here that finds the driver's the first time it is called: in libOpenGL, else through
eglGetProcAddress, else by the name without its ARB or EXT ending. The CGL calls and the few functions
system_gl.c answers itself are left out.
"""
import re
import sys

ENTRY = re.compile(r"^RS_GL_(FN|VOID)\((.*)\)\s*$")
OWN = {"glGetString", "glGetIntegerv", "glBindVertexArrayAPPLE", "glGenVertexArraysAPPLE", "glDeleteVertexArraysAPPLE"}


def entries(text):
    """(return type, name, parameters, arguments) of each table line."""
    out = []
    for line in text.splitlines():
        m = ENTRY.match(line)
        if not m:
            continue
        kind, body = m.groups()
        # ret, name, (params), (args): the two parenthesized groups are the last two
        m2 = re.match(r"^(?:(?P<ret>.*?),\s*)?(?P<name>\w+),\s*\((?P<params>.*)\),\s*\((?P<args>[^()]*)\)$", body)
        if not m2:
            raise ValueError("cannot read the table line: " + line)
        out.append((m2.group("ret") or "void" if kind == "FN" else "void", m2.group("name"), m2.group("params"),
                    m2.group("args")))
    return out


def source(table):
    out = ["// Made by linux/gl/gen_system_gl.py from libRSOpenGL's table (gl_api.inc). Not to be edited.",
           "#include <OpenGL/OpenGL.h>", "",
           "void *rs_system_gl_find(const char *name);  // system_gl.c", ""]
    seen = set()
    for ret, name, params, args in table:
        if name in seen or name in OWN or name.startswith("CGL"):
            continue
        seen.add(name)
        call = "real(%s)" % args
        out.append("__attribute__((visibility(\"default\"))) %s %s(%s) {" % (ret, name, params))
        out.append("  static __typeof__(%s) *real;" % name)
        out.append("  if (!real) real = (__typeof__(%s) *)rs_system_gl_find(\"%s\");" % (name, name))
        out.append("  %s%s;" % ("" if ret == "void" else "return ", call))
        out.append("}")
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1]) as f:
        table = entries(f.read())
    with open(argv[2], "w") as f:
        f.write(source(table))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
