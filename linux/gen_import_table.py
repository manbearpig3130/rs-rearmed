#!/usr/bin/env python3
"""linux/darwin-imports.tsv as a C table for the loader (linux/src/imports.c): each import's class, found by name.

    python3 linux/gen_import_table.py OUT.inc     (linux/Makefile runs it)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import import_census as census  # noqa: E402


def c_string(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def table(rows):
    """The C text. The rows are sorted by symbol, then library, as the loader's search expects (bytes, as strcmp)."""
    classes = list(census.CLASSES)
    out = ["// Made by linux/gen_import_table.py from linux/darwin-imports.tsv: edit that.",
           "#define RSL_NCLASSES %d" % len(classes),
           "#define RSL_CLASS_SAME %d" % classes.index("same"),
           "#define RSL_CLASS_SHIM %d" % classes.index("shim"),
           "static const char *const rsl_classes[RSL_NCLASSES] = {%s};" % ", ".join(c_string(c) for c in classes),
           "static const struct rsl_row {",
           "  const char *symbol, *library;",
           "  unsigned char cls, call;",
           "} rsl_rows[] = {"]
    for (lib, sym), (kind, cls, _how) in sorted(rows.items(), key=lambda kv: (kv[0][1].encode(), kv[0][0].encode())):
        out.append("    {%s, %s, %d, %d}," % (c_string(sym), c_string(lib), classes.index(cls), kind == "call"))
    out += ["};", "#define RSL_NROWS (sizeof rsl_rows / sizeof *rsl_rows)", ""]
    return "\n".join(out)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    try:
        text = table(census.read_table())
    except census.Failure as e:
        sys.stderr.write("gen_import_table: %s\n" % e)
        return 1
    with open(argv[1], "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
