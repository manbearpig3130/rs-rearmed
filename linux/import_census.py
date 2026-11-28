#!/usr/bin/env python3
"""What the Mac game takes from macOS: every import of its image, by library, against the table that says what a
Linux build does about each (linux/darwin-imports.tsv).

    import_census.py [IMAGE]            the counts, by library and by class
    import_census.py [IMAGE] --check    exit 1 unless the table and the image name the same imports
    import_census.py [IMAGE] --list     one line per import: library, symbol, kind, class
    import_census.py [IMAGE] --missing  rows for the imports the table lacks, class "todo", to paste and fill in

IMAGE is the decrypted x86-64 image (tools/steamless/decrypt.py), default build/steamless/Rocksmith2014_x64_decrypted
or $RC_IMAGE. Without an image only the table is read (--check then checks the table alone).

An import is a symbol the image's dyld info binds from another library: `call` when it has a lazy pointer (the
game calls it through a stub, and the build makes a thunk for it), `data` when only a plain pointer is bound (a
variable, a class, a vtable, or a function kept as a pointer). Symbols the image defines itself (weak instances of
inline C++) are not imports and are left out, as tools/ws7/recomp/thunks/gen_thunks.py leaves them out.

The Mach-O is read here, with no Apple tool, so the census runs on Linux too: thunks/imports.py asks
`xcrun objdump --macho` for the same tables. Standard library only.
"""
import argparse
import collections
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TABLE = os.path.join(HERE, "darwin-imports.tsv")
DEFAULT_IMAGE = os.path.join(ROOT, "build", "steamless", "Rocksmith2014_x64_decrypted")

FAT_MAGIC, MH_MAGIC_64, CPU_X86_64 = 0xcafebabe, 0xfeedfacf, 0x01000007
LC_REQ_DYLD = 0x80000000
LC_SEGMENT_64, LC_DYLD_INFO, LC_DYLD_INFO_ONLY = 0x19, 0x22, 0x22 | LC_REQ_DYLD
LC_SYMTAB, LC_DYSYMTAB, S_LAZY_SYMBOL_POINTERS = 0x2, 0xb, 0x7
LC_DYLIBS = (0xc, 0x18 | LC_REQ_DYLD, 0x1f | LC_REQ_DYLD, 0x23 | LC_REQ_DYLD)  # load, weak, reexport, upward

# What a Linux build does about an import (the table's third column).
CLASSES = collections.OrderedDict([
    ("same", "the host's own function or object of that name: glibc, libm, libgcc, GNU libstdc++"),
    ("adapt", "a function of ours with the Darwin prototype over the host's: constants, struct layouts, errno"),
    ("emulate", "a Darwin-only facility written anew: Mach, OSAtomic, sysctl, the rune tables, vDSP, CoreFoundation"),
    ("runtime", "already the recompiler runtime's own (rc_file, rc_setjmp), which has to build on Linux"),
    ("shim", "one of the project's replacement libraries, built for Linux: the renderer, Cg, Bink, Steam"),
    ("layer", "part of a stand-in for a macOS service, over the Linux shell or audio backend"),
    ("refuse", "the network: answered as with no network at all, the offline rule"),
    ("stub", "nothing the app runs calls it (the game's own main, Cocoa shell and ARC support code did): logs and fails"),
    ("todo", "not looked at yet"),
])


class Failure(Exception):
    pass


def uleb(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7f) << s
        s += 7
        if not c & 0x80:
            return r, i


def cstr(b, i):
    j = b.index(b"\0", i)
    return b[i:j].decode("ascii", "replace"), j + 1


def binds(stream, segs, lazy):
    """(address, ordinal, symbol, addend) of every bind in a dyld bind opcode stream. `segs` are the segments'
    addresses."""
    i, ordinal, sym, addr, addend = 0, 0, None, 0, 0
    while i < len(stream):
        op, imm = stream[i] & 0xf0, stream[i] & 0x0f
        i += 1
        if op == 0x00:    # DONE: ends a lazy entry, and the whole of the other streams
            if not lazy:
                break
        elif op == 0x10:  # SET_DYLIB_ORDINAL_IMM
            ordinal = imm
        elif op == 0x20:  # SET_DYLIB_ORDINAL_ULEB
            ordinal, i = uleb(stream, i)
        elif op == 0x30:  # SET_DYLIB_SPECIAL_IMM: 0 self, -1 main executable, -2 flat lookup
            ordinal = (imm | 0xf0) - 256 if imm else 0
        elif op == 0x40:  # SET_SYMBOL_TRAILING_FLAGS_IMM
            sym, i = cstr(stream, i)
        elif op == 0x50:  # SET_TYPE_IMM
            pass
        elif op == 0x60:  # SET_ADDEND_SLEB
            addend, shift = 0, 0
            while True:
                c = stream[i]
                i += 1
                addend |= (c & 0x7f) << shift
                shift += 7
                if not c & 0x80:
                    if c & 0x40:
                        addend -= 1 << shift
                    break
        elif op == 0x70:  # SET_SEGMENT_AND_OFFSET_ULEB
            off, i = uleb(stream, i)
            addr = segs[imm] + off
        elif op == 0x80:  # ADD_ADDR_ULEB
            off, i = uleb(stream, i)
            addr = (addr + off) & (2 ** 64 - 1)
        elif op == 0x90:  # DO_BIND
            yield addr, ordinal, sym, addend
            addr += 8
        elif op == 0xa0:  # DO_BIND_ADD_ADDR_ULEB
            yield addr, ordinal, sym, addend
            off, i = uleb(stream, i)
            addr = (addr + 8 + off) & (2 ** 64 - 1)
        elif op == 0xb0:  # DO_BIND_ADD_ADDR_IMM_SCALED
            yield addr, ordinal, sym, addend
            addr += 8 + imm * 8
        elif op == 0xc0:  # DO_BIND_ULEB_TIMES_SKIPPING_ULEB
            n, i = uleb(stream, i)
            skip, i = uleb(stream, i)
            for _ in range(n):
                yield addr, ordinal, sym, addend
                addr += 8 + skip
        else:
            raise Failure("unknown bind opcode 0x%02x" % (op | imm))


def library_name(path):
    """`OpenGL` of .../OpenGL.framework/Versions/A/OpenGL, `libSystem` of /usr/lib/libSystem.B.dylib."""
    name = path.rsplit("/", 1)[-1]
    if name.endswith(".dylib"):
        name = name[:-6]
        head, _, tail = name.rpartition(".")
        if head and (tail.isdigit() or len(tail) == 1):  # libstdc++.6, libSystem.B, libobjc.A
            name = head
    return name


def x86_64_slice(data):
    if struct.unpack_from(">I", data)[0] == FAT_MAGIC:
        for k in range(struct.unpack_from(">I", data, 4)[0]):
            cpu, _, off, size, _ = struct.unpack_from(">5I", data, 8 + 20 * k)
            if cpu == CPU_X86_64:
                return data[off:off + size]
        raise Failure("no x86-64 slice")
    return data


def read_image(path):
    """{(library, symbol): kind} of the image's imports, kind `call` or `data`."""
    with open(path, "rb") as f:
        data = x86_64_slice(f.read())
    magic, cpu, _, _, ncmds, _, _, _ = struct.unpack_from("<8I", data)
    if magic != MH_MAGIC_64 or cpu != CPU_X86_64:
        raise Failure("%s is not an x86-64 Mach-O" % path)
    segs, dylibs, info, symtab, indirect, lazy_ptrs, off = [], [], None, None, None, [], 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == LC_SEGMENT_64:
            segs.append(struct.unpack_from("<Q", data, off + 24)[0])
            for k in range(struct.unpack_from("<I", data, off + 64)[0]):
                sect = off + 72 + 80 * k
                ssize, flags, first = struct.unpack_from("<Q", data, sect + 40)[0], *struct.unpack_from("<II", data, sect + 64)
                if flags & 0xff == S_LAZY_SYMBOL_POINTERS:
                    lazy_ptrs.append((first, ssize // 8))
        elif cmd in LC_DYLIBS:
            name_off = struct.unpack_from("<I", data, off + 8)[0]
            dylibs.append(library_name(cstr(data, off + name_off)[0]))
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            info = struct.unpack_from("<10I", data, off + 8)
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack_from("<4I", data, off + 8)      # symoff, nsyms, stroff, strsize
        elif cmd == LC_DYSYMTAB:
            indirect = struct.unpack_from("<II", data, off + 56)   # indirectsymoff, nindirectsyms
        off += size
    if info is None or symtab is None or indirect is None:
        raise Failure("%s has no dyld info or symbol tables (a newer link format than the game's)" % path)
    # What has a lazy pointer is called through a stub: the names of the lazy pointers' indirect symbols.
    called = set()
    for first, n in lazy_ptrs:
        for k in range(first, min(first + n, indirect[1])):
            idx = struct.unpack_from("<I", data, indirect[0] + 4 * k)[0]
            if idx < symtab[1]:  # not INDIRECT_SYMBOL_LOCAL or _ABS
                strx = struct.unpack_from("<I", data, symtab[0] + 16 * idx)[0]
                called.add(cstr(data, symtab[2] + strx)[0])
    (_, _, bind_off, bind_size, _, _, lazy_off, lazy_size, _, _) = info
    # The weak binds are left out: they name what dyld coalesces, and a symbol only they name is the image's own
    # copy (a weak instance of inline C++). One a library has too (operator new) is in the other two as well.
    out = {}
    for stream_off, stream_size, lazy in ((bind_off, bind_size, False), (lazy_off, lazy_size, True)):
        for _, ordinal, sym, _ in binds(data[stream_off:stream_off + stream_size], segs, lazy):
            if 1 <= ordinal <= len(dylibs):
                out[(dylibs[ordinal - 1], sym)] = "call" if sym in called else "data"
    return out


def read_table(path=TABLE):
    """{(library, symbol): (kind, class, how)} of darwin-imports.tsv."""
    rows = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            p = line.split("\t")
            if len(p) < 4:
                raise Failure("%s:%d: library, symbol, kind and class, tab-separated" % (path, n))
            lib, sym, kind, cls = p[:4]
            if kind not in ("call", "data"):
                raise Failure("%s:%d: kind is call or data, not %r" % (path, n, kind))
            if cls not in CLASSES:
                raise Failure("%s:%d: no class %r (%s)" % (path, n, cls, ", ".join(CLASSES)))
            if (lib, sym) in rows:
                raise Failure("%s:%d: %s %s is there twice" % (path, n, lib, sym))
            rows[(lib, sym)] = (kind, cls, p[4] if len(p) > 4 else "")
    return rows


def compare(image, table):
    """What --check fails on: (missing from the table, in the table but not imported, kind differs)."""
    missing = sorted(k for k in image if k not in table)
    gone = sorted(k for k in table if k not in image)
    kind = sorted(k for k in image if k in table and table[k][0] != image[k])
    return missing, gone, kind


def report(image, table, out=None):
    out = out or sys.stdout
    keys = image if image is not None else {k: v[0] for k, v in table.items()}
    libs = collections.OrderedDict()
    for (lib, sym), kind in keys.items():
        row = libs.setdefault(lib, collections.Counter())
        row[kind] += 1
        row[table[(lib, sym)][1] if (lib, sym) in table else "todo"] += 1
    used = [c for c in CLASSES if any(r[c] for r in libs.values())]
    head = "%-22s %5s %5s  " % ("library", "call", "data") + " ".join("%7s" % c for c in used)
    out.write(head + "\n")
    total = collections.Counter()
    for lib in sorted(libs, key=lambda l: -(libs[l]["call"] + libs[l]["data"])):
        r = libs[lib]
        total.update(r)
        out.write("%-22s %5d %5d  " % (lib, r["call"], r["data"]) + " ".join("%7s" % (r[c] or "") for c in used) + "\n")
    out.write("%-22s %5d %5d  " % ("total", total["call"], total["data"]) + " ".join("%7d" % total[c] for c in used) + "\n\n")
    for c in used:
        out.write("%-8s %4d  %s\n" % (c, total[c], CLASSES[c]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", nargs="?", default=os.environ.get("RC_IMAGE") or DEFAULT_IMAGE)
    ap.add_argument("--table", default=TABLE)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--missing", action="store_true")
    a = ap.parse_args(argv)
    try:
        table = read_table(a.table) if os.path.exists(a.table) else {}
        image = read_image(a.image) if os.path.exists(a.image) else None
    except Failure as e:
        sys.stderr.write("import_census: %s\n" % e)
        return 2
    if image is None:
        sys.stderr.write("import_census: no image at %s: the table alone\n" % a.image)
    if a.missing:
        for lib, sym in sorted(k for k in (image or {}) if k not in table):
            print("%s\t%s\t%s\ttodo\t" % (lib, sym, image[(lib, sym)]))
        return 0
    if a.list:
        keys = image if image is not None else {k: v[0] for k, v in table.items()}
        for lib, sym in sorted(keys):
            kind, cls, how = table.get((lib, sym), (keys[(lib, sym)], "todo", ""))
            print("%s\t%s\t%s\t%s\t%s" % (lib, sym, keys[(lib, sym)], cls, how))
        return 0
    report(image, table)
    if not a.check:
        return 0
    bad = 0
    if image is not None:
        missing, gone, kind = compare(image, table)
        for what, keys in (("not in the table", missing), ("in the table, not imported", gone),
                           ("another kind in the table", kind)):
            for lib, sym in keys:
                sys.stderr.write("import_census: %s %s: %s\n" % (lib, sym, what))
                bad += 1
    todo = sum(1 for v in table.values() if v[1] == "todo")
    if todo:
        sys.stderr.write("import_census: %d imports are still todo\n" % todo)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
