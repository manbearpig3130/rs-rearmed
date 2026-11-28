// rsl: the Mac game's image in an x86-64 Linux process, run as it is (linux/X86-64.md).
//
// The game is an x86-64 program with the calling convention Linux has too, and its code makes no system call of its
// own: everything it wants of a system it asks through 681 imports. rsl_load() maps the decrypted image
// (tools/steamless's output) at its own address and writes an answer into every import's slot:
//
//   - the host's own function or object of that name, where linux/darwin-imports.tsv says `same`;
//   - the Darwin layer's (linux/darwin), where it has one;
//   - one of the project's replacement libraries built for Linux (the Steam library, later the renderer, Cg and
//     Bink), where the table says `shim` and the library is beside this program;
//   - else an address in a region with no access, one per import, so that the first call or read of it stops the
//     run and names it.
//
// Nothing is translated and nothing in the image is changed: with no slide there is nothing to rebase.
#ifndef RSL_H
#define RSL_H

#include <stdint.h>
#include <stdio.h>

enum { RSL_HOST, RSL_LAYER, RSL_SHIM, RSL_NONE };  // who answers an import

typedef struct {
  const char *library, *symbol;  // `libSystem`, `_pthread_key_create`: as the table writes them
  const char *cls;               // the table's class, or "?" for an import the table lacks
  int how;                       // RSL_HOST, RSL_LAYER, RSL_SHIM or RSL_NONE
  int called;                    // the image calls it (a lazy pointer) as well as, or instead of, holding its address
  uint64_t at;                   // the answer
  uint64_t stub;                 // with counting on: what the lazy pointer holds instead, a few counting instructions
} rsl_import;

typedef struct {
  uint64_t lo, hi;            // the image's addresses
  uint64_t text_lo, text_hi;  // its __TEXT
  int ninits;                 // static initializers (__mod_init_func)
  int unwind;                 // 1: its unwind tables are registered with the host's unwinder
} rsl_info;

// 0, or -1 with the reason on stderr. With COUNT_CALLS every call of an import is counted (rsl_import_calls).
int rsl_load(const char *path, int count_calls, rsl_info *info);

// Runs the static initializers in the image's order, each as dyld calls it. Returns how many ran: all of them, as
// one that faults or reaches an import with no answer ends the process (exit status 3 or 4, the reason on stderr).
int rsl_run_initializers(int argc, char **argv, char **envp);
// How many initializers have a frame description the host's unwinder finds: a C++ throw unwinds through those.
int rsl_initializers_with_unwind(void);

// A survey, to see what a part of the game asks for in one run: a call of a function with no answer is noted
// (stderr, once each) and returns 0 instead of ending the run. What the game does after that proves nothing.
void rsl_survey(int on);
void rsl_report_survey(FILE *out);
int rsl_survey_note(const rsl_import *im);  // 1 the first time

// --- the shell (shell.c, where SDL 3 is there to build with) ---
typedef struct {
  const char *game_dir;   // the game's folder: the Mac depot as Steam downloaded it
  int width, height;      // the window's size; 0: 1280 x 720
  long frames;            // stop after this many; 0: run until the game quits
  int survey;             // rsl_survey for the whole run
  const char *picture;    // save frame number picture_frame there (a PPM file)
  long picture_frame;
  long picture_every;     // or every so many frames, as <picture>.<frame>.ppm
  const char *keys;       // keys typed by a script: "600:return,900:down" (frame:key)
} rsl_shell_options;
int rsl_shell_run(const rsl_shell_options *opt);  // the window, the game's start and its frames; an exit status
// A function of one of the project's libraries, by the Mac library's name ("OpenGL") and the function's.
void *rsl_shim_function(const char *library, const char *name);

// --- sound (audio_pipewire.c, where PipeWire is there to build with) ---
void rsl_audio_pipewire(void);  // the game's sound goes to PipeWire from the next start of its audio graph
// An ALSA card's capture level, 0 to 1 over its control's range (audio_alsa.c, where ALSA is there to build
// with). Each returns 1 when the card has the control.
int rsl_alsa_level_get(int card, float *level);
int rsl_alsa_level_set(int card, float level);

// --- native functions in the place of game functions (patch.c) ---
struct rc_ctx;
// The game function at ADDRESS becomes a jump to NATIVE, an override as rc_ov.h has them. 0, or -1.
int rsl_override(uint64_t address, void (*native)(struct rc_ctx *), const char *name);
// The override modules this build has (`online`: the game stays offline; the built-in fixes' replaced
// functions). How many functions, or -1.
int rsl_install_overrides(void);
// A hook point (rc_hook.h) at the function at ADDRESS, with the index rc_hook.c knows it by. 0, or -1.
int rsl_hook_point(uint64_t address, int index, const char *name);
// Every hook point of the tables. How many were put in.
int rsl_install_hooks(void);
// The mid points (rc_mid.h) that have a hook. How many were put in.
int rsl_install_mids(void);
uint64_t rsl_image_end(void);  // the end of the loaded image (loader.c)
void rsl_direct_image(uint64_t lo, uint64_t hi);  // the runtime's variables for where the image is (direct.c)

// --- the imports (imports.c) ---
void rsl_imports_begin(int count_calls);
rsl_import *rsl_answer(const char *library, const char *symbol, int lazy);
void rsl_imports_end(void);                      // the counting stubs become executable
const rsl_import *rsl_import_at(uint64_t addr);  // the unanswered import whose no-access address this is, or NULL
const rsl_import *rsl_imports(int *n);
uint64_t rsl_import_calls(const rsl_import *im);
void rsl_report_binding(FILE *out);
void rsl_report_calls(FILE *out);

#endif
