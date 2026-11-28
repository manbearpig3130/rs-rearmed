#!/usr/bin/env python3
"""The Mac game's own x86-64 code, run without translating it: its image mapped, its imports answered and its static
initializers run, in this Python process. No compiler and nothing to install: the first thing to try on an x86-64
Linux machine, before any of the port is built (linux/X86-64.md, linux/PLAN.md X1).

    first_run.py IMAGE              run the initializers and say which imports they called
    first_run.py IMAGE --first N    only the first N of them
    first_run.py IMAGE --json FILE  the counts as JSON as well
    first_run.py --machine          what the plan asks of this machine (Linux; no image needed)

IMAGE is the decrypted image, from your own copy of the game: `python3 tools/steamless/decrypt.py --game DIR`
(default build/steamless/Rocksmith2014_x64_decrypted, or $RC_IMAGE).

Where an import comes from:
  Linux    linux/darwin-imports.tsv: a `same` row is the host's own function (glibc, libm, GNU libstdc++); a few
           others have a stand-in here, written in Python and good for a single thread only (STANDINS); every
           other import stops the run and names itself.
  macOS    an Intel process (`arch -x86_64 /usr/bin/python3 linux/first_run.py IMAGE`): macOS's own libSystem,
           libstdc++ and CoreFoundation, as the differential harness binds them (shims/game/harness/rsimage.c).
           The reference: what the initializers call when everything is real.

Each call of an import goes through a few instructions that count it, so the report is what the initializers reach.
The initializers run in a child process: one that dies takes only the child, and the parent says which it was.

Standard library only. Exit status: 0 every initializer ran, 1 one stopped (an unanswered import, or a fault),
2 the image or the machine is not what this needs.
"""
import argparse
import collections
import ctypes
import json
import os
import platform
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import import_census as census  # noqa: E402

LINUX, MAC = sys.platform.startswith("linux"), sys.platform == "darwin"
PROT_RWX, MAP_PRIVATE, MAP_SHARED = 7, 2, 1
MAP_ANON = 0x20 if LINUX else 0x1000
MAP_FIXED_NOREPLACE = 0x100000  # Linux: at this address or not at all
MAC_SLIDE = 0x300000000         # Rosetta's own runtime sits at the image's address (rsimage.h)
LC_SEGMENT_64, LC_DYLD_INFO, LC_DYLD_INFO_ONLY = census.LC_SEGMENT_64, census.LC_DYLD_INFO, census.LC_DYLD_INFO_ONLY
S_MOD_INIT_FUNC_POINTERS = 0x9
STUB = 32                       # bytes a counting stub or a trap stub takes
U64 = ctypes.c_uint64

libc = ctypes.CDLL(None, use_errno=True)
libc.mmap.restype = ctypes.c_void_p
libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]


class Failure(Exception):
    pass


def mmap_at(addr, size, flags=MAP_PRIVATE):
    """SIZE bytes, readable, writable and executable, at ADDR exactly (0: anywhere)."""
    if LINUX and addr:
        flags |= MAP_FIXED_NOREPLACE
    p = libc.mmap(addr, size, PROT_RWX, flags | MAP_ANON, -1, 0)
    if p in (None, 2 ** 64 - 1):
        raise Failure("no executable memory (mmap: errno %d): on Linux, does SELinux deny execmem?" % ctypes.get_errno())
    if addr and p != addr:
        raise Failure("0x%x is taken in this process (got 0x%x)" % (addr, p))
    return p


# --- the image ----------------------------------------------------------------------------------------------

class Image:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = d = census.x86_64_slice(f.read())
        magic, cpu, _, _, ncmds, _, _, _ = struct.unpack_from("<8I", d)
        if magic != census.MH_MAGIC_64 or cpu != census.CPU_X86_64:
            raise Failure("%s is not an x86-64 Mach-O" % path)
        self.segs, self.dylibs, self.info, self.inits_at = [], [], None, None
        off = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", d, off)
            if cmd == LC_SEGMENT_64:
                name = d[off + 8:off + 24].rstrip(b"\0").decode()
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<4Q", d, off + 24)
                self.segs.append((name, vmaddr, vmsize, fileoff, filesize))
                for k in range(struct.unpack_from("<I", d, off + 64)[0]):
                    s = off + 72 + 80 * k
                    addr, ssize = struct.unpack_from("<QQ", d, s + 32)
                    if struct.unpack_from("<I", d, s + 64)[0] & 0xff == S_MOD_INIT_FUNC_POINTERS:
                        self.inits_at = (addr, ssize // 8)
            elif cmd in census.LC_DYLIBS:
                self.dylibs.append(census.library_name(census.cstr(d, off + struct.unpack_from("<I", d, off + 8)[0])[0]))
            elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
                self.info = struct.unpack_from("<10I", d, off + 8)
            off += size
        if self.info is None or self.inits_at is None:
            raise Failure("%s has no dyld info or no initializers: not the game's image" % path)
        self.lo = min(v for n, v, s, _, _ in self.segs if s and n != "__PAGEZERO")
        self.hi = max(v + s for n, v, s, _, _ in self.segs if s and n != "__PAGEZERO")

    def stream(self, k):
        off, size = self.info[2 * k], self.info[2 * k + 1]  # 0 rebase, 1 bind, 2 weak bind, 3 lazy bind, 4 export
        return self.data[off:off + size]

    def exports(self):
        """{name: address} of the image's own exported symbols (the export trie)."""
        trie, out, todo = self.stream(4), {}, [(0, "")]
        if not trie:
            return out
        base = [v for n, v, _, _, _ in self.segs if n == "__TEXT"][0]
        while todo:
            at, name = todo.pop()
            size, p = census.uleb(trie, at)
            if size:
                flags, q = census.uleb(trie, p)
                if not flags & 0x18:  # not a re-export, not a stub with a resolver
                    out[name] = base + census.uleb(trie, q)[0]
            p += size
            count, p = trie[p], p + 1
            for _ in range(count):
                edge, p = census.cstr(trie, p)
                child, p = census.uleb(trie, p)
                todo.append((child, name + edge))
        return out

    def rebases(self, segs):
        """The addresses the rebase opcodes name."""
        b, i, addr = self.stream(0), 0, 0
        while i < len(b):
            op, imm = b[i] & 0xf0, b[i] & 0x0f
            i += 1
            if op == 0x00:
                return
            elif op == 0x10:      # SET_TYPE_IMM
                pass
            elif op == 0x20:      # SET_SEGMENT_AND_OFFSET_ULEB
                off, i = census.uleb(b, i)
                addr = segs[imm] + off
            elif op == 0x30:      # ADD_ADDR_ULEB
                off, i = census.uleb(b, i)
                addr = (addr + off) & (2 ** 64 - 1)
            elif op == 0x40:      # ADD_ADDR_IMM_SCALED
                addr += imm * 8
            elif op == 0x50:      # DO_REBASE_IMM_TIMES
                for _ in range(imm):
                    yield addr
                    addr += 8
            elif op == 0x60:      # DO_REBASE_ULEB_TIMES
                n, i = census.uleb(b, i)
                for _ in range(n):
                    yield addr
                    addr += 8
            elif op == 0x70:      # DO_REBASE_ADD_ADDR_ULEB
                yield addr
                off, i = census.uleb(b, i)
                addr = (addr + 8 + off) & (2 ** 64 - 1)
            elif op == 0x80:      # DO_REBASE_ULEB_TIMES_SKIPPING_ULEB
                n, i = census.uleb(b, i)
                skip, i = census.uleb(b, i)
                for _ in range(n):
                    yield addr
                    addr += 8 + skip
            else:
                raise Failure("unknown rebase opcode 0x%02x" % (op | imm))


# --- what answers an import ---------------------------------------------------------------------------------

MAC_LIBS = ("libSystem", "libstdc++", "CoreFoundation")  # what the harness binds; a lookup in libobjc can fault


def host_libraries():
    """The libraries an import may come from here, in the order they are asked."""
    if MAC:  # as shims/game/harness/rsimage.c binds
        names = ["/usr/lib/libSystem.B.dylib", "/usr/lib/libstdc++.6.dylib",
                 "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"]
    else:
        names = [None, "libm.so.6", "libstdc++.so.6", "libgcc_s.so.1"]
    libs = []
    for n in names:
        try:
            libs.append(ctypes.CDLL(n, use_errno=True))
        except OSError as e:
            raise Failure("cannot load %s: %s" % (n, e))
    return libs


def host_symbol(libs, name):
    """The address of NAME in the host's libraries, or None."""
    for lib in libs:
        try:
            return ctypes.cast(getattr(lib, name), ctypes.c_void_p).value
        except AttributeError:
            pass
    return None


def host_name(sym):
    """A Mach-O symbol as the host spells it: no leading underscore; on Linux no `$INODE64` and the like."""
    name = sym[1:] if sym.startswith("_") else sym
    return name if MAC else name.split("$")[0]


class Cells:
    """Zeroed memory for the data imports nothing real stands behind."""

    def __init__(self, size=1 << 20):
        self.base, self.used, self.size = mmap_at(0, size), 0, size

    def take(self, n, value=b""):
        n = (n + 63) & ~63
        if self.used + n > self.size:
            raise Failure("out of placeholder memory")
        at = self.base + self.used
        self.used += n
        if value:
            ctypes.memmove(at, value, len(value))
        return at


# Darwin's ctype bits (<_ctype.h>): the image's inlined isalpha and the like read them from _DefaultRuneLocale.
CT_A, CT_C, CT_D, CT_G, CT_L, CT_P, CT_S, CT_U, CT_X, CT_B, CT_R = (
    0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000)
RUNE_TYPES, RUNE_LOWER, RUNE_UPPER, RUNE_SIZE = 60, 60 + 1024, 60 + 2048, 4096  # offsets in Darwin's _RuneLocale


def rune_locale():
    """Darwin's _DefaultRuneLocale for the C locale: the type bits and case maps of the first 256 characters."""
    b = bytearray(RUNE_SIZE)
    b[0:8] = b"RuneMagA"
    b[8:12] = b"NONE"
    struct.pack_into("<I", b, 56, 0xfffd)  # the invalid rune
    for c in range(256):
        t = 0
        if c < 128:
            ch = chr(c)
            if c < 32 or c == 127:
                t |= CT_C
            if ch in " \t\n\v\f\r":
                t |= CT_S
            if ch in " \t":
                t |= CT_B
            if 32 <= c < 127:
                t |= CT_R
            if 33 <= c < 127:
                t |= CT_G
            if ch.isdigit():
                t |= CT_D | CT_X | (c - 48)
            elif ch.isalpha():
                t |= CT_A | (CT_U if ch.isupper() else CT_L)
                if ch.lower() in "abcdef":
                    t |= CT_X | (ord(ch.lower()) - 87)
            elif 33 <= c < 127:
                t |= CT_P
        struct.pack_into("<I", b, RUNE_TYPES + 4 * c, t)
        struct.pack_into("<I", b, RUNE_LOWER + 4 * c, ord(chr(c).lower()) if c < 128 else c)
        struct.pack_into("<I", b, RUNE_UPPER + 4 * c, ord(chr(c).upper()) if c < 128 else c)
    return bytes(b)


# Linux: the imports beyond `same` that the initializers call, each with a stand-in good enough for them: one
# thread, nothing asked of the machine. The port's own layer is C (linux/PLAN.md X1); these only let the first run
# get through. name -> (result type, argument types, function); a string is the host function to call as it is.
def standins(libs):
    c_int, c_void_p, c_char_p = ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p
    err = host_symbol(libs, "__errno_location")

    def atomic_add32(amount, where):
        cell = ctypes.c_int32.from_address(where)
        cell.value += amount
        return cell.value

    def cas32(old, new, where):
        cell = ctypes.c_int32.from_address(where)
        if cell.value != old:
            return 0
        cell.value = new
        return 1

    def key_create(where, destructor):
        key = ctypes.c_uint(0)
        r = libs[0].pthread_key_create(ctypes.byref(key), ctypes.c_void_p(destructor))
        U64.from_address(where).value = key.value  # Darwin's key is 8 bytes
        return r

    def mutexattr_settype(attr, kind):  # Darwin: 0 normal, 1 error check, 2 recursive; glibc: 0, 2, 1
        return libs[0].pthread_mutexattr_settype(ctypes.c_void_p(attr), {1: 2, 2: 1}.get(kind, kind))

    def sysctlbyname(name, oldp, oldlenp, newp, newlen):
        if err:
            errno_at = ctypes.CFUNCTYPE(ctypes.c_void_p)(err)()
            ctypes.c_int.from_address(errno_at).value = 2  # ENOENT, as an Apple silicon Mac answers a name it lacks
        return -1

    return {
        "___error": "__errno_location",          # Linux's numbers: enough while nothing fails
        "_mach_absolute_time": (ctypes.c_uint64, [], lambda: int(__import__("time").monotonic() * 1e9)),
        "_OSAtomicAdd32": (c_int, [c_int, c_void_p], atomic_add32),
        "_OSAtomicIncrement32": (c_int, [c_void_p], lambda w: atomic_add32(1, w)),
        "_OSAtomicDecrement32": (c_int, [c_void_p], lambda w: atomic_add32(-1, w)),
        "_OSAtomicCompareAndSwap32": (c_int, [c_int, c_int, c_void_p], cas32),
        "_pthread_key_create": (c_int, [c_void_p, c_void_p], key_create),
        "_pthread_mutexattr_settype": (c_int, [c_void_p, c_int], mutexattr_settype),
        "_pthread_mutex_lock": "pthread_mutex_lock", "_pthread_mutex_unlock": "pthread_mutex_unlock",
        "_pthread_mutex_trylock": "pthread_mutex_trylock", "_pthread_mutex_destroy": "pthread_mutex_destroy",
        "_sysctlbyname": (c_int, [c_char_p, c_void_p, c_void_p, c_void_p, ctypes.c_size_t], sysctlbyname),
        "___bzero": "bzero", "_pthread_yield_np": "sched_yield",
        # macOS answers 15 and 47 for every policy
        "_sched_get_priority_min": (c_int, [c_int], lambda policy: 15),
        "_sched_get_priority_max": (c_int, [c_int], lambda policy: 47),
        "_setjmp": "_setjmp", "_longjmp": "longjmp", "_close": "close", "_read": "read", "_fopen": "fopen",
    }


class Binder:
    """Answers the image's imports, and counts the calls of those it calls."""

    def __init__(self, image, table):
        self.libs = host_libraries()
        self.table = table
        self.cells = Cells()
        self.keep = []                 # ctypes callbacks, alive as long as the image runs
        self.imports = []              # (library, symbol, how it is answered) of every called import
        self.index = {}
        self.unanswered_data = []
        n = 4096
        self.stubs = mmap_at(0, n * STUB)
        self.counts_at = libc.mmap(0, n * 8, 3, MAP_SHARED | MAP_ANON, -1, 0)  # the parent reads them after the child
        self.counts = (U64 * n).from_address(self.counts_at)
        self.trap = ctypes.CFUNCTYPE(None, ctypes.c_int)(self.on_trap)
        self.trap_at = ctypes.cast(self.trap, ctypes.c_void_p).value
        self.report_trap = None
        self.standins = standins(self.libs) if LINUX else {}
        self.data = self.linux_data() if LINUX else {}

    def linux_data(self):
        c = self.cells
        d = {"___stack_chk_guard": c.take(8, os.urandom(7) + b"\0"), "_mach_task_self_": c.take(4, struct.pack("<I", 0x103)),
             "__DefaultRuneLocale": c.take(RUNE_SIZE, rune_locale())}
        for darwin, host in (("___stdinp", "stdin"), ("___stdoutp", "stdout"), ("___stderrp", "stderr")):
            d[darwin] = host_symbol(self.libs, host)
        return d

    def on_trap(self, i):
        if self.report_trap:
            self.report_trap(i)
        os._exit(3)

    def stub(self, lib, sym, target, how):
        """A few instructions at a fixed place: count the call, then go to TARGET, or stop the run when it is None."""
        i = len(self.imports)
        self.imports.append((lib, sym, how))
        at = self.stubs + i * STUB
        code = b"\x49\xbb" + struct.pack("<Q", self.counts_at + 8 * i) + b"\xf0\x49\xff\x03"   # lock inc qword [r11]
        if target is None:
            code = b"\xbf" + struct.pack("<I", i) + b"\x49\xbb" + struct.pack("<Q", self.trap_at) + b"\x41\xff\xe3"
        else:
            code += b"\x49\xbb" + struct.pack("<Q", target) + b"\x41\xff\xe3"                    # jmp r11
        ctypes.memmove(at, code, len(code))
        return at

    def function(self, lib, sym):
        """Where calls of this import go: its stub."""
        if (lib, sym) in self.index:
            return self.index[(lib, sym)]
        cls = self.table.get((lib, sym), ("call", "todo", ""))[1]
        target, how = None, "unanswered (%s)" % cls
        if cls == "same" if LINUX else lib in MAC_LIBS:
            target = host_symbol(self.libs, host_name(sym))
            how = "the host's" if target else ("unanswered: same, but the host has no %s" % host_name(sym)) if LINUX else how
        if target is None and sym in self.standins:
            s = self.standins[sym]
            if isinstance(s, str):
                target = host_symbol(self.libs, s)
            else:
                cb = ctypes.CFUNCTYPE(s[0], *s[1])(s[2])
                self.keep.append(cb)
                target = ctypes.cast(cb, ctypes.c_void_p).value
            how = "a stand-in (%s)" % cls
        at = self.index[(lib, sym)] = self.stub(lib, sym, target, how)
        return at

    def datum(self, lib, sym):
        """Where a data import points: the host's object, one made here, or zeroed memory."""
        if sym in self.data:
            return self.data[sym]
        cls = self.table.get((lib, sym), ("data", "todo", ""))[1]
        if cls == "same" if LINUX else lib in MAC_LIBS:
            at = host_symbol(self.libs, host_name(sym))
            if at:
                return at
        if sym == "dyld_stub_binder":
            return self.stub(lib, sym, None, "unanswered (stub)")
        self.unanswered_data.append((lib, sym))
        at = self.data[sym] = self.cells.take(256)
        return at


def load(image, binder, called):
    """The image in memory at its place: copied, rebased, bound. Returns the slide."""
    slide = MAC_SLIDE if MAC else 0
    mmap_at(image.lo + slide, (image.hi - image.lo + 0xfff) & ~0xfff)
    for name, vmaddr, vmsize, fileoff, filesize in image.segs:
        if filesize and name != "__PAGEZERO":
            ctypes.memmove(vmaddr + slide, image.data[fileoff:fileoff + filesize], filesize)
    segs = [v + slide for _, v, _, _, _ in image.segs]
    if slide:
        for at in image.rebases(segs):
            U64.from_address(at).value += slide
    for k, lazy in ((1, False), (3, True)):
        for at, ordinal, sym, addend in census.binds(image.stream(k), segs, lazy):
            if not 1 <= ordinal <= len(image.dylibs):
                continue
            lib = image.dylibs[ordinal - 1]
            to = binder.function(lib, sym) if sym in called else binder.datum(lib, sym)
            U64.from_address(at).value = (to + addend) & (2 ** 64 - 1)
    own = image.exports()
    for at, _, sym, addend in census.binds(image.stream(2), segs, False):  # weak: the image's own copy wins
        if sym in own:
            U64.from_address(at).value = (own[sym] + slide + addend) & (2 ** 64 - 1)
    return slide


def called_symbols(image_path):
    return {sym for (lib, sym), kind in census.read_image(image_path).items() if kind == "call"}


# --- the run ------------------------------------------------------------------------------------------------

def run(image_path, first=None):
    """Runs the initializers in a child. Returns (ran, total, why it stopped or None, binder)."""
    image = Image(image_path)
    table = census.read_table() if os.path.exists(census.TABLE) else {}
    binder = Binder(image, table)
    slide = load(image, binder, called_symbols(image_path))
    at, total = image.inits_at
    inits = list((U64 * total).from_address(at + slide))
    if first is not None:
        inits = inits[:first]
    argv = (ctypes.c_char_p * 2)(b"Rocksmith2014", None)
    envp = (ctypes.c_char_p * 1)(None)
    apple = (ctypes.c_char_p * 2)(b"executable_path=Rocksmith2014", None)
    kind = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
    sys.stdout.flush()
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        binder.report_trap = lambda i: os.write(w, b"trap %d\n" % i)
        try:
            import faulthandler
            faulthandler.disable()
            for i, fn in enumerate(inits):
                os.write(w, b"%d\n" % i)
                kind(fn)(1, argv, envp, apple)
            os.write(w, b"done\n")
        finally:
            os._exit(0)  # not exit(): the game's own exit handlers stay unrun
    os.close(w)
    last, trap, done, buf = -1, None, False, b""
    while True:
        chunk = os.read(r, 65536)
        if not chunk:
            break
        buf += chunk
    for line in buf.split(b"\n"):
        if line == b"done":
            done = True
        elif line.startswith(b"trap "):
            trap = int(line[5:])
        elif line:
            last = int(line)
    _, status = os.waitpid(pid, 0)
    why = None
    if not done:
        where = "initializer %d of %d, at 0x%x" % (last + 1, len(inits), inits[last] - slide) if last >= 0 else "the start"
        if trap is not None:
            lib, sym, how = binder.imports[trap]
            why = "%s called %s %s: %s" % (where, lib, sym, how)
        elif os.WIFSIGNALED(status):
            why = "%s died with signal %d" % (where, os.WTERMSIG(status))
        else:
            why = "%s ended the process (status %d)" % (where, os.WEXITSTATUS(status))
    ran = len(inits) if done else max(last, 0)
    return ran, len(inits), why, binder


def report(ran, total, why, binder, out=None):
    out = out or sys.stdout
    rows = [(binder.counts[i], lib, sym, how) for i, (lib, sym, how) in enumerate(binder.imports) if binder.counts[i]]
    out.write("%d of %d initializers ran%s\n" % (ran, total, "" if why is None else "; stopped: " + why))
    by = collections.Counter()
    for n, lib, sym, how in rows:
        cls = binder.table.get((lib, sym), ("", "?", ""))[1]
        by[cls] += 1
    out.write("imports they called: %d  (%s)\n" % (len(rows), ", ".join("%s %d" % kv for kv in sorted(by.items()))))
    for n, lib, sym, how in sorted(rows, key=lambda r: (binder.table.get((r[1], r[2]), ("", "?", ""))[1] == "same", -r[0])):
        cls = binder.table.get((lib, sym), ("", "?", ""))[1]
        if cls != "same":
            out.write("  %9d  %-10s %-34s %-8s %s\n" % (n, lib, sym, cls, how if LINUX else ""))
    same = [(n, sym) for n, lib, sym, how in rows if binder.table.get((lib, sym), ("", "?", ""))[1] == "same"]
    out.write("  and %d `same` imports, %d calls, the most called: %s\n"
              % (len(same), sum(n for n, _ in same), ", ".join("%s %d" % (s, n) for n, s in sorted(same, reverse=True)[:6])))
    if LINUX and binder.unanswered_data:
        out.write("data imports with zeroed memory behind them: %d\n" % len(binder.unanswered_data))
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", nargs="?", default=os.environ.get("RC_IMAGE") or census.DEFAULT_IMAGE)
    ap.add_argument("--first", type=int)
    ap.add_argument("--json")
    ap.add_argument("--machine", action="store_true")
    a = ap.parse_args(argv)
    if a.machine:
        return machine()
    if platform.machine() not in ("x86_64", "AMD64"):
        sys.stderr.write("first_run: this process is %s: the game's code is x86-64 (on an Apple silicon Mac: "
                         "arch -x86_64 /usr/bin/python3 %s)\n" % (platform.machine(), " ".join(sys.argv)))
        return 2
    if not os.path.exists(a.image):
        sys.stderr.write("first_run: no image at %s (tools/steamless/decrypt.py makes it from your game)\n" % a.image)
        return 2
    try:
        ran, total, why, binder = run(a.image, a.first)
    except (Failure, census.Failure) as e:
        sys.stderr.write("first_run: %s\n" % e)
        return 2
    rows = report(ran, total, why, binder)
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"ran": ran, "total": total, "stopped": why, "host": sys.platform,
                       "called": [{"library": lib, "symbol": sym, "calls": n, "how": how} for n, lib, sym, how in rows]},
                      f, indent=1)
    return 0 if why is None else 1


# --- the machine ---------------------------------------------------------------------------------------------

def tool(*cmd, lines=40):
    """The output of a program, or None when it is not there or fails."""
    import subprocess
    try:
        r = subprocess.run(list(cmd), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None
    text = r.stdout.decode("utf-8", "replace").strip()
    return "\n".join(text.split("\n")[:lines]) if r.returncode == 0 and text else None


def home(path):
    """PATH with the home folder as `~`, also where the home folder is a link (/home to /var/home on an image-based system)."""
    for h in (os.path.expanduser("~"), os.path.realpath(os.path.expanduser("~"))):
        if path == h or path.startswith(h + os.sep):
            return "~" + path[len(h):]
    return path


def steam_roots():
    """Steam's folders that exist, each once."""
    seen, out = set(), []
    for p in ("~/.local/share/Steam", "~/.steam/steam", "~/.steam/root",
              "~/.var/app/com.valvesoftware.Steam/.local/share/Steam", "~/snap/steam/common/.local/share/Steam"):
        real = os.path.realpath(os.path.expanduser(p))
        if os.path.isdir(real) and real not in seen:
            seen.add(real)
            out.append(real)
    return out


def steam_libraries(root):
    """The library folders a Steam root names (libraryfolders.vdf), itself first."""
    import re
    libs = [root]
    try:
        with open(os.path.join(root, "steamapps", "libraryfolders.vdf"), encoding="utf-8", errors="replace") as f:
            for m in re.finditer(r'"path"\s+"([^"]+)"', f.read()):
                path = m.group(1).replace("\\\\", "\\")
                if os.path.isdir(path) and os.path.realpath(path) not in [os.path.realpath(x) for x in libs]:
                    libs.append(path)
    except OSError:
        pass
    return libs


def steam_facts(say):
    roots = steam_roots()
    if not roots:
        say("Steam: no Steam folder found in the usual places")
        return
    for root in roots:
        say("Steam: %s%s" % (home(root), " (Flatpak)" if "/.var/app/" in root else ""))
        for lib in steam_libraries(root):
            acf = os.path.join(lib, "steamapps", "appmanifest_221680.acf")
            if os.path.exists(acf):
                import re
                with open(acf, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                depots = re.findall(r'"(2216\d\d)"\s*\{', text)
                say("  Rocksmith 2014 is installed in %s: depots %s (221682 is the Mac game's)"
                    % (home(lib), ", ".join(depots) or "not listed"))
        # download_depot's folder is under Steam's runtime folder (steamrt64 on Bazzite in 2026; ubuntu12_32 before)
        import glob
        for content in sorted(glob.glob(os.path.join(root, "*", "steamapps", "content"))) + [os.path.join(root, "steamapps", "content")]:
            depot = os.path.join(content, "app_221680", "depot_221682")
            if os.path.isdir(depot):
                exe = os.path.join(depot, "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014")
                say("  the Mac depot is downloaded: %s (%s)" % (home(depot), "with the game's program" if os.path.exists(exe)
                                                                else "the program is not there yet"))
            elif os.path.isdir(content):
                say("  Steam's download folder: %s (no Mac depot in it yet)" % home(content))


def opengl_facts(say):
    """Asks EGL for an OpenGL compatibility context without a window, and reads what the context reports."""
    c_void_p, c_int = ctypes.c_void_p, ctypes.c_int
    WANT = ("GL_ARB_vertex_program", "GL_ARB_fragment_program", "GL_EXT_gpu_program_parameters",
            "GL_EXT_texture_compression_s3tc", "GL_EXT_bgra", "GL_ARB_get_program_binary")
    try:
        egl = ctypes.CDLL("libEGL.so.1")
        gl = None
        for name in ("libGL.so.1", "libOpenGL.so.0"):
            try:
                gl = ctypes.CDLL(name)
                break
            except OSError:
                pass
        if gl is None:
            say("OpenGL: no libGL here")
            return
        for fn, res, args in (("eglGetDisplay", c_void_p, [c_void_p]), ("eglInitialize", c_int, [c_void_p, c_void_p, c_void_p]),
                              ("eglBindAPI", c_int, [ctypes.c_uint]),
                              ("eglChooseConfig", c_int, [c_void_p, c_void_p, c_void_p, c_int, c_void_p]),
                              ("eglCreateContext", c_void_p, [c_void_p, c_void_p, c_void_p, c_void_p]),
                              ("eglCreatePbufferSurface", c_void_p, [c_void_p, c_void_p, c_void_p]),
                              ("eglMakeCurrent", c_int, [c_void_p, c_void_p, c_void_p, c_void_p]),
                              ("eglTerminate", c_int, [c_void_p])):
            getattr(egl, fn).restype, getattr(egl, fn).argtypes = res, args
        gl.glGetString.restype, gl.glGetString.argtypes = ctypes.c_char_p, [ctypes.c_uint]
        ints = lambda *v: (c_int * len(v))(*v)
        displays = [("the session's display", lambda: egl.eglGetDisplay(None), True)]
        try:  # Mesa's display with no window system at all, where the first has no offscreen surface to give
            egl.eglGetPlatformDisplay.restype, egl.eglGetPlatformDisplay.argtypes = c_void_p, [ctypes.c_uint, c_void_p, c_void_p]
            displays.append(("a surfaceless display", lambda: egl.eglGetPlatformDisplay(0x31DD, None, None), False))
        except AttributeError:
            pass
        notes = []
        for label, open_display, pbuffer in displays:
            dpy = open_display()
            major, minor = c_int(), c_int()
            if not dpy or not egl.eglInitialize(dpy, ctypes.byref(major), ctypes.byref(minor)):
                notes.append("%s: EGL does not start" % label)
                continue
            cfg, n = c_void_p(), c_int()
            # EGL_SURFACE_TYPE: a pbuffer, or none at all (left out, EGL asks for a window, which surfaceless has not)
            want = ints(0x3033, 0x0001 if pbuffer else 0, 0x3040, 0x0008, 0x3038)
            if not egl.eglBindAPI(0x30A2) or not egl.eglChooseConfig(dpy, want, ctypes.byref(cfg), 1, ctypes.byref(n)) or not n.value:
                notes.append("%s: no config for desktop OpenGL" % label)
                egl.eglTerminate(dpy)
                continue
            done = False
            for what, attrs in (("a compatibility context", ints(0x30FD, 0x0002, 0x3038)), ("a context with no profile asked", ints(0x3038))):
                ctx = egl.eglCreateContext(dpy, cfg, None, attrs)
                surf = egl.eglCreatePbufferSurface(dpy, cfg, ints(0x3057, 16, 0x3056, 16, 0x3038)) if ctx and pbuffer else None
                if not ctx or not egl.eglMakeCurrent(dpy, surf, surf, ctx):
                    notes.append("%s: no %s" % (label, what[2:]))
                    continue
                get = lambda e: (gl.glGetString(e) or b"").decode("utf-8", "replace")
                ext = set(get(0x1F03).split())
                say("OpenGL (%s, %s): %s, GLSL %s" % (label, what, get(0x1F02), get(0x8B8C)))
                say("  %s / %s" % (get(0x1F00), get(0x1F01)))
                say("  the game's needs: " + ", ".join("%s %s" % (w[3:], "yes" if w in ext else "NO") for w in WANT)
                    + ("" if ext else " (no extension list: a core context)"))
                done = True
                break
            egl.eglTerminate(dpy)
            if done:
                return
        say("OpenGL: EGL gave no context (%s); `glxinfo -B` or `eglinfo -B` would say more" % "; ".join(notes))
    except Exception as e:  # nothing here may stop the report
        say("OpenGL: could not ask EGL (%s: %s)" % (type(e).__name__, e))


def machine(out=None):
    """What the plan's open questions ask of this machine. Reads only; changes and installs nothing."""
    out = out or sys.stdout
    say = lambda line: out.write(line + "\n")
    say("RS Re-ARMed, Linux port: what this machine has (no account names; it does name the GPU and audio devices)")
    try:
        rel = {}
        with open("/etc/os-release") as f:
            for line in f:
                k, _, v = line.strip().partition("=")
                rel[k] = v.strip('"')
        distro = rel.get("PRETTY_NAME", "?") + (" (an image-based system)" if os.path.exists("/run/ostree-booted") else "")
    except OSError:
        distro = platform.platform()
    say("System: %s; kernel %s; %s; pages of %d bytes; Python %s; C library %s"
        % (distro, platform.release(), platform.machine(), os.sysconf("SC_PAGE_SIZE"), platform.python_version(),
           " ".join(platform.libc_ver()) or "?"))
    shown = os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")
    say("Session: %s on %s%s" % (os.environ.get("XDG_SESSION_TYPE", "?"), os.environ.get("XDG_CURRENT_DESKTOP", "?"),
                                "" if shown or not LINUX else " (no display in this shell: a terminal on the desktop"
                                " answers for the session itself)"))
    if platform.machine() not in ("x86_64", "AMD64"):
        say("The game's code: not runnable as it is here (%s); this machine would need the translated build" % platform.machine())
    # where the image wants to be, and executable memory
    try:
        at = mmap_at(0x100000000 if LINUX else 0, 0x1a00000)
        say("The image's place: %s, and memory may be made executable"
            % ("0x100000000 is free" if LINUX else "not asked on this system (0x%x given)" % at))
        libc.munmap(ctypes.c_void_p(at), ctypes.c_size_t(0x1a00000))
    except Failure as e:
        say("The image's place: %s" % e)
    # the table's `same` rows against this machine's libraries
    try:
        libs = host_libraries()
        table = census.read_table()
        same = sorted(k for k, v in table.items() if v[1] == "same")
        missing = [k for k in same if host_symbol(libs, host_name(k[1])) is None]
        cxx = [k for k in same if k[0] == "libstdc++"]
        say("Imports the host answers itself: %d of the table's %d `same` rows found (GNU libstdc++: %d of %d)"
            % (len(same) - len(missing), len(same), len(cxx) - len([k for k in missing if k[0] == "libstdc++"]), len(cxx)))
        for lib, sym in missing[:40]:
            say("  not here: %s %s" % (lib, sym))
    except (Failure, census.Failure, OSError) as e:
        say("Imports the host answers itself: could not tell (%s)" % e)
    if LINUX:
        opengl_facts(say)
    for name, cmd in (("glxinfo", ("glxinfo", "-B")), ("vulkaninfo", ("vulkaninfo", "--summary"))):
        text = tool(*cmd, lines=200)
        if text:
            keep = [l.strip() for l in text.split("\n") if any(k in l for k in ("OpenGL vendor", "OpenGL renderer",
                    "OpenGL version", "OpenGL core profile version", "deviceName", "apiVersion", "driverName"))]
            say("%s: %s" % (name, "; ".join(keep[:6]) or "ran"))
    # sound
    pw = tool("pipewire", "--version") or tool("pw-cli", "--version")
    say("Sound: %s" % (pw.replace("\n", "; ") if pw else "no pipewire program found"))
    settings = tool("pw-metadata", "-n", "settings", "0", lines=30)
    if settings:
        import re
        vals = dict(re.findall(r"key:'(clock\.[a-z.-]+)' value:'([^']*)'", settings))
        say("  clock: rate %s, quantum %s (min %s, max %s), forced %s" % tuple(
            vals.get("clock." + k, "?") for k in ("rate", "quantum", "min-quantum", "max-quantum", "force-quantum")))
    for lib in ("libpipewire-0.3.so.0", "libjack.so.0", "libasound.so.2", "libSDL3.so.0"):
        try:
            ctypes.CDLL(lib)
            say("  %s: here" % lib)
        except OSError:
            say("  %s: not here" % lib)
    cards = tool("arecord", "-l", lines=30)
    if cards:
        for line in cards.split("\n"):
            if line.startswith("card "):
                say("  input: " + line)
    steam_facts(say)
    # AppImage and a place to build
    fuse = [n for n in ("fusermount3", "fusermount") if tool("sh", "-c", "command -v " + n)]
    say("AppImage: /dev/fuse %s; %s" % ("is there" if os.path.exists("/dev/fuse") else "is NOT there",
                                       ", ".join(fuse) or "no fusermount"))
    have = [n for n in ("distrobox", "toolbox", "podman", "docker", "clang", "gcc", "cmake", "git")
            if tool("sh", "-c", "command -v " + n)]
    say("To build with: %s" % (", ".join(have) or "none of distrobox, toolbox, podman, clang, gcc"))
    try:
        st = os.statvfs(os.path.expanduser("~"))
        say("Room in the home folder: %.0f GB free (the Mac game is 7 GB, and DLC 16 MB each)" % (st.f_bavail * st.f_frsize / 1e9))
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
