// What the loader reads of a Mach-O file: Linux has no <mach-o/loader.h>. Names and numbers are the format's own.
#ifndef RSL_MACHO_H
#define RSL_MACHO_H

#include <stdint.h>

#define MH_MAGIC_64 0xfeedfacfu
#define CPU_TYPE_X86_64 0x01000007u

#define LC_SEGMENT_64 0x19u
#define LC_LOAD_DYLIB 0xcu
#define LC_LOAD_WEAK_DYLIB 0x80000018u
#define LC_REEXPORT_DYLIB 0x8000001fu
#define LC_LAZY_LOAD_DYLIB 0x20u
#define LC_LOAD_UPWARD_DYLIB 0x80000023u
#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY 0x80000022u

#define SECTION_TYPE 0xffu
#define S_LAZY_SYMBOL_POINTERS 0x7u
#define S_MOD_INIT_FUNC_POINTERS 0x9u

struct mach_header_64 {
  uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};

struct load_command {
  uint32_t cmd, cmdsize;
};

struct segment_command_64 {
  uint32_t cmd, cmdsize;
  char segname[16];
  uint64_t vmaddr, vmsize, fileoff, filesize;
  uint32_t maxprot, initprot, nsects, flags;
};

struct section_64 {
  char sectname[16], segname[16];
  uint64_t addr, size;
  uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2, reserved3;
};

struct dylib_command {
  uint32_t cmd, cmdsize, name, timestamp, current_version, compatibility_version;
};

struct dyld_info_command {
  uint32_t cmd, cmdsize;
  uint32_t rebase_off, rebase_size, bind_off, bind_size, weak_bind_off, weak_bind_size;
  uint32_t lazy_bind_off, lazy_bind_size, export_off, export_size;
};

#define BIND_OPCODE_MASK 0xf0u
#define BIND_IMMEDIATE_MASK 0x0fu
#define BIND_OPCODE_DONE 0x00u
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM 0x10u
#define BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB 0x20u
#define BIND_OPCODE_SET_DYLIB_SPECIAL_IMM 0x30u
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40u
#define BIND_OPCODE_SET_TYPE_IMM 0x50u
#define BIND_OPCODE_SET_ADDEND_SLEB 0x60u
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70u
#define BIND_OPCODE_ADD_ADDR_ULEB 0x80u
#define BIND_OPCODE_DO_BIND 0x90u
#define BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB 0xa0u
#define BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED 0xb0u
#define BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB 0xc0u

#endif
