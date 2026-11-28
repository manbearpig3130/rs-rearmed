# The x86-64 Linux build: the Mac game's own code, loaded and run as it is (X86-64.md, PLAN.md's X1).
#
#   make -C linux           build/linux/rs-rearmed
#   make -C linux check     the Darwin layer's own test; and the game's initializers, when the image is there
#
# It needs a C compiler, make and Python, and of the system the C library's headers alone.
OUT ?= ../build/linux
PYTHON ?= python3
CFLAGS ?= -O2 -g
IMAGE ?= $(or $(RC_IMAGE),../build/steamless/Rocksmith2014_x64_decrypted)

ifneq ($(shell uname -sm),Linux x86_64)
$(error This builds for x86-64 Linux alone, where the game's code runs as it is: see linux/README.md)
endif

# -fno-strict-aliasing: the layer reads and writes the game's objects through its own types.
ALL_CFLAGS = -std=gnu11 -Wall -Wextra -fno-strict-aliasing -pthread $(CFLAGS) $(ALL_CFLAGS_EXTRA) -Isrc -Idarwin -I$(OUT) -I$(OUT)/target
LIBS = -ldl -lm -pthread -lstdc++

LAYER = $(sort $(wildcard darwin/*.c))
SRC = src/main.c src/loader.c src/imports.c $(LAYER)
OBJ = $(SRC:%.c=$(OUT)/%.o)

TARGET = ../tools/targets

# The app's native code that touches the game, from its own sources, against the recompiler runtime's own headers
# (src/direct.c has the runtime's functions as they are where nothing is translated; src/patch.c puts a native
# function in a game function's place). So far the `online` override module: the game stays offline.
RUNTIME = ../tools/ws7/recomp/runtime
OVERRIDES = ../tools/ws7/recomp/overrides
GAME = ../shims/game
NATIVE_CFLAGS = -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas -Wno-comment \
    -Wno-missing-field-initializers -Wno-format-truncation -Wno-attributes \
    -fno-strict-aliasing -pthread $(CFLAGS) -Isrc -Icompat -include compat/rs_linux.h -I$(OUT)/target -I$(RUNTIME) \
    -I$(OVERRIDES) -I$(OVERRIDES)/gen -I$(GAME)/include -I../shims/include
NATIVE_OBJ = $(OUT)/native/direct.o $(OUT)/native/patch.o $(OUT)/native/online_glue.o $(OUT)/native/online.o \
    $(OUT)/native/online_stub.o $(OUT)/native/rc_hook.o $(OUT)/native/rc_mid.o $(OUT)/native/rc_target.o $(OUT)/native/compat.o \
    $(OUT)/native/audio_in.o $(OUT)/native/audio_ring.o $(OUT)/native/audio_glue.o $(OUT)/native/rc_mods.o \
    $(OUT)/native/rc_file.o \
    $(MODS_SRC:$(OVERRIDES)/mods/%.c=$(OUT)/native/mods_%.o)
# The built-in fixes (overrides/mods), compiled as they are, with the runtime's hook points (rc_hook.c) and the
# target's tables by name (rc_target.c, which targets.py writes beside rc_target_names.h).
MODS_SRC = $(sort $(wildcard $(OVERRIDES)/mods/*.c))
$(OUT)/target/hooks.inc: $(TARGET)/names/hooks.list
	@mkdir -p $(OUT)/target
	sed -n 's/^\([a-z][a-z0-9_]*\).*/HOOK(\1)/p' $< > $@
# (-O0, and for rc_mid.c below: each has weak, constant defaults for the tables another file gives it, the hook
# points' among them; optimizing, gcc reads a default's value where it is used, and the points would number none.)
$(OUT)/native/rc_hook.o: $(RUNTIME)/rc_hook.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -O0 -c -o $@ $<
# (rc_edit_enable there is the translated build's, where an edit group is a switch the translated code reads: here
# it would say "on" and change nothing. The program has its own, src/patch.c.)
$(OUT)/native/rc_mid.o: $(RUNTIME)/rc_mid.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -O0 -Drc_edit_enable=rc_edit_enable_translated -c -o $@ $<
$(OUT)/native/rc_file.o: $(RUNTIME)/rc_file.c $(RUNTIME)/rc_file.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/native/rc_mods.o: $(RUNTIME)/rc_mods.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/target/mids.inc: gen_mids.py patch-sites.tsv $(TARGET)/mac-x64/mids.map
	@mkdir -p $(OUT)/target
	$(PYTHON) gen_mids.py $@
$(OUT)/native/rc_target.o: $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $(OUT)/target/rc_target.c
$(OUT)/native/compat.o: compat/compat.c $(wildcard compat/*.h compat/*/*.h)
	@mkdir -p $(OUT)/native
	$(CC) -O2 -g -Wall -Wextra -Icompat -c -o $@ $<
$(OUT)/native/mods_%.o: $(OVERRIDES)/mods/%.c $(wildcard $(OVERRIDES)/mods/*.h) $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -I$(OVERRIDES)/mods -I../shims/platform/include -I../shims/coreaudio -I../shims/opengl/src -c -o $@ $<
$(OUT)/target/overrides_online.inc: $(TARGET)/names/overrides/online.list
	@mkdir -p $(OUT)/target
	sed -n 's/^\([a-z][a-z0-9_]*\).*/OV(\1)/p' $< > $@
# The `audio` override module's functions that are not the Mac's alone: the input queue's reader and the sink's
# ring (its third, the engine thread's real-time policy in the device's workgroup, is macOS's).
$(OUT)/target/overrides_audio.inc: $(TARGET)/names/overrides/audio.list
	@mkdir -p $(OUT)/target
	sed -n 's/^\(input_queue_read\|ak_sink_ring_init\)[ \t].*/OV(\1)/p' $< > $@
$(OUT)/native/audio_%.o: $(OVERRIDES)/src/audio_%.c $(OVERRIDES)/src/audio.h $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -I../shims/coreaudio -c -o $@ $<
$(OUT)/native/audio_glue.o: src/audio_glue.c $(OVERRIDES)/src/audio.h $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -I$(OVERRIDES)/src -I../shims/coreaudio -c -o $@ $<
$(OUT)/native/direct.o: src/direct.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/native/patch.o: src/patch.c src/rsl.h $(OUT)/target/rc_target_names.h $(OUT)/target/overrides_online.inc $(OUT)/target/hooks.inc \
    $(OUT)/target/overrides_audio.inc \
    $(OUT)/target/mids.inc
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/native/online_glue.o: $(OVERRIDES)/src/online_glue.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/native/online.o: $(OVERRIDES)/gen/online.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/native/online_stub.o: $(GAME)/src/online_stub.cpp $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CXX) -std=c++17 -O2 -g -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter -Icompat -include compat/rs_linux.h \
	    -I$(OUT)/target -I$(RUNTIME) -I$(GAME)/include -I../shims/include -c -o $@ $<
LAYER_OBJ = $(LAYER:%.c=$(OUT)/%.o)

# The guitar input's policy, the Mac app's own source (shims/coreaudio/rs_audio_input.c: the settings file, the
# list's entries, the level), compiled as it is with compat's CoreAudio names; the audio layer uses it.
POLICY = ../shims/coreaudio
POLICY_OBJ = $(OUT)/darwin/rs_audio_input.o
$(POLICY_OBJ): $(POLICY)/rs_audio_input.c $(POLICY)/rs_audio_input.h $(POLICY)/rs_audio_host.h $(wildcard compat/*/*.h compat/*.h)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -fno-strict-aliasing -pthread $(CFLAGS) -Icompat -I$(POLICY) -c -o $@ $<
$(OUT)/darwin/dw_audio.o: ALL_CFLAGS += -Icompat -I$(POLICY)
OBJ += $(POLICY_OBJ)
LAYER_OBJ += $(POLICY_OBJ)

# The shell (the window, the frame loop, input) is in the program where SDL 3 is there to build with: the Mac
# shell's C half and the app's seam from their own sources, and src/shell.c.
SDL_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null)
ifneq ($(SDL_LIBS),)
PLATFORM = ../shims/platform
APP = ../tools/ws7/recomp/app
# (-Wno-comment and the two after it: the shared sources are written for clang on the Mac)
SHELL_CFLAGS = -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-comment -Wno-unused-variable -Wno-format-truncation \
    -fno-strict-aliasing -pthread $(CFLAGS) $(shell pkg-config --cflags sdl3) \
    -DRSL_HAVE_SHELL -Isrc -Idarwin -Icompat -include compat/rs_linux.h -I$(OUT)/target -I$(RUNTIME) -I$(PLATFORM)/include -I$(PLATFORM)/src -I$(APP)
SHELL_OBJ = $(OUT)/shell/shell.o $(OUT)/shell/seam_platform.o $(OUT)/shell/rsp_engine.o $(OUT)/shell/rsp_logic.o \
    $(OUT)/shell/rsp_paths.o $(OUT)/shell/rsp_config.o
ALL_CFLAGS_EXTRA = -DRSL_HAVE_SHELL
# Sound, where PipeWire is there to build with: the game's audio graph to a PipeWire stream.
PW_LIBS := $(shell pkg-config --libs libpipewire-0.3 2>/dev/null)
ifneq ($(PW_LIBS),)
ALL_CFLAGS_EXTRA += -DRSL_HAVE_PIPEWIRE
SHELL_OBJ += $(OUT)/shell/audio_pipewire.o
$(OUT)/shell/audio_pipewire.o: src/audio_pipewire.c src/rsl.h darwin/dw.h
	@mkdir -p $(OUT)/shell
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -pthread $(CFLAGS) $(shell pkg-config --cflags libpipewire-0.3) \
	    $(PW_ALSA_FLAG) -Isrc -Idarwin -c -o $@ $<
# The cable's level, where ALSA is there to build with: its card's capture control.
ALSA_LIBS := $(shell pkg-config --libs alsa 2>/dev/null)
ifneq ($(ALSA_LIBS),)
PW_ALSA_FLAG = -DRSL_HAVE_ALSA
PW_LIBS += $(ALSA_LIBS)
SHELL_OBJ += $(OUT)/shell/audio_alsa.o
$(OUT)/shell/audio_alsa.o: src/audio_alsa.c src/rsl.h
	@mkdir -p $(OUT)/shell
	$(CC) -std=gnu11 -Wall -Wextra -pthread $(CFLAGS) $(shell pkg-config --cflags alsa) -Isrc -c -o $@ $<
endif
endif
$(OUT)/shell/shell.o: src/shell.c src/rsl.h $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/shell
	$(CC) $(SHELL_CFLAGS) -c -o $@ $<
$(OUT)/shell/seam_platform.o: $(APP)/seam_platform.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/shell
	$(CC) $(SHELL_CFLAGS) -c -o $@ $<
$(OUT)/shell/rsp_%.o: $(PLATFORM)/src/rsp_%.c $(wildcard $(PLATFORM)/include/*.h)
	@mkdir -p $(OUT)/shell
	$(CC) $(SHELL_CFLAGS) -c -o $@ $<
endif

all: $(OUT)/rs-rearmed $(OUT)/libsteam_api.so

# The renderer's libraries need OpenGL's and EGL's headers (on Bazzite: in a distrobox): `make -C linux gl`.
gl: $(OUT)/libRSSystemGL.so $(OUT)/libRSOpenGL.so $(OUT)/libCg.so

# (The run path: a packaged build has its own SDL 3 beside the program, for a system without one. -rdynamic: the
# mods the program loads call its functions: the hook points, the game's names, calls into the game.)
$(OUT)/rs-rearmed: $(OBJ) $(NATIVE_OBJ) $(SHELL_OBJ)
	$(CC) $(LDFLAGS) -rdynamic -o $@ $(OBJ) $(NATIVE_OBJ) $(SHELL_OBJ) $(LIBS) $(SDL_LIBS) $(PW_LIBS) -Wl,-rpath,'$$ORIGIN'

# A mod, as the mods folder holds them: a shared library that names the game's points (runtime/rc_mod.h) and finds
# the program's functions when it is loaded (the program is linked to offer them: -rdynamic). The test's own.
$(OUT)/test/mods/%.so: test/mods/%.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -Wall -Wextra -O2 -g -shared -fPIC -I$(RUNTIME) -I$(OVERRIDES) -I$(OUT)/target -o $@ $<

# The project's replacement libraries, from the sources the Mac app builds them from (shims/), each a shared
# library beside the program. compat/ has the few macOS functions they call.
SHIM_CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -fno-exceptions -Icompat
STEAM = ../shims/steam/src
COMPAT_H = $(wildcard compat/*.h compat/*/*.h)
$(OUT)/shims/compat.o: compat/compat.c $(COMPAT_H)
	@mkdir -p $(OUT)/shims
	$(CC) -O2 -g -Wall -Wextra -fPIC -fvisibility=hidden -Icompat -c -o $@ compat/compat.c

$(OUT)/libsteam_api.so: $(STEAM)/shim.cpp $(STEAM)/interfaces.cpp $(STEAM)/shim.h $(STEAM)/enumerate.h $(OUT)/shims/compat.o
	$(CXX) $(SHIM_CXXFLAGS) -shared -o $@ $(STEAM)/shim.cpp $(STEAM)/interfaces.cpp $(OUT)/shims/compat.o -ldl -pthread

# --- the renderer: libRSOpenGL and Cg from their own sources (shims/opengl, shims/cg), over libRSSystemGL ---
# libRSSystemGL.so is Mesa as libRSOpenGL's OpenGL backend expects Apple's OpenGL to be (gl/system_gl.c).
# rs_linux.h, included first, maps the two places the shared code names a macOS library.
GLDIR = $(OUT)/gl
OGL = ../shims/opengl
CGS = ../shims/cg/src
# (-Wno-unused-function, -Wno-unknown-pragmas: the shared sources are written for clang on the Mac)
GL_CFLAGS = -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas \
    -Icompat -include compat/rs_linux.h \
    -I$(GLDIR) -I$(OGL)/src

$(GLDIR)/system_gl_table.c: $(GLDIR)/gl_api.inc gl/gen_system_gl.py
	$(PYTHON) gl/gen_system_gl.py $(GLDIR)/gl_api.inc $@

$(OUT)/libRSSystemGL.so: gl/system_gl.c $(GLDIR)/system_gl_table.c $(COMPAT_H)
	$(CC) -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Icompat -shared -o $@ gl/system_gl.c $(GLDIR)/system_gl_table.c \
	    -lOpenGL -lEGL -ldl -pthread

# The wrapper table and the enum names, by the Mac build's own generators, from Khronos's headers.
$(GLDIR)/headers/gl.h: gl/khronos_headers.py $(wildcard compat/OpenGL/*.h)
	@mkdir -p $(GLDIR)
	$(PYTHON) gl/khronos_headers.py $(GLDIR)/headers

$(GLDIR)/gl_api.inc: $(GLDIR)/headers/gl.h $(OGL)/tools/gen-gl-api.py $(OGL)/game-gl-imports.txt $(OGL)/extra-gl-imports.txt
	$(PYTHON) $(OGL)/tools/gen-gl-api.py $(OGL)/game-gl-imports.txt $(OGL)/extra-gl-imports.txt $(GLDIR)/headers \
	    $(GLDIR)/gl_api.inc $(GLDIR)/rsmtl_stubs.c

$(GLDIR)/gl_enums.inc: $(GLDIR)/headers/gl.h $(OGL)/tools/gen-gl-enums.py
	$(PYTHON) $(OGL)/tools/gen-gl-enums.py $(GLDIR)/headers $@

OGL_SRC = $(OGL)/src/rsgl.c $(OGL)/src/gltrace.c $(OGL)/src/rs_gfx.c
$(OUT)/libRSOpenGL.so: $(OGL_SRC) $(wildcard $(OGL)/src/*.h) $(GLDIR)/gl_api.inc $(GLDIR)/gl_enums.inc $(OUT)/shims/compat.o
	$(CC) -std=gnu11 $(GL_CFLAGS) -shared -o $@ $(OGL_SRC) $(GLDIR)/rsmtl_stubs.c $(OUT)/shims/compat.o -ldl -lm -pthread

# --- videos: the Bink library from its own source (shims/bink), over the same FFmpeg with the same patch ---
# `make -C linux bink` downloads FFmpeg's source (checked against the Mac build's checksum) and builds it: a few
# minutes the first time. It needs curl, patch and xz beside the compiler.
BINK = ../shims/bink
FFMPEG = $(OUT)/ffmpeg
$(FFMPEG)/lib/libavcodec.a: bink/build-ffmpeg.sh $(BINK)/build-ffmpeg.sh $(BINK)/third_party/ffmpeg-bink2.patch
	CC=$(CC) bink/build-ffmpeg.sh $(FFMPEG)

$(GLDIR)/bink.map: $(BINK)/exports.txt
	@mkdir -p $(GLDIR)
	sed -n 's/^_\(.*\)/\1;/p' $< | { echo '{ global:'; cat; echo 'local: *; };'; } > $@

$(OUT)/libBink2.so: $(BINK)/bink_shim.c $(BINK)/bink2_abi.h $(FFMPEG)/lib/libavcodec.a $(GLDIR)/bink.map $(COMPAT_H)
	$(CC) -std=gnu11 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Icompat -include compat/rs_linux.h \
	    -I$(FFMPEG)/include -shared -o $@ $(BINK)/bink_shim.c -Wl,--version-script=$(GLDIR)/bink.map -Wl,-Bsymbolic \
	    -Wl,--exclude-libs,ALL $(FFMPEG)/lib/libavformat.a $(FFMPEG)/lib/libavcodec.a $(FFMPEG)/lib/libavutil.a -lm -pthread

bink: $(OUT)/libBink2.so

# Only the Cg API is exported, as the Mac's library exports it (cg.exports: the same names with an underscore).
$(GLDIR)/cg.map: $(CGS)/cg.exports
	@mkdir -p $(GLDIR)
	sed -n 's/^_\(.*\)/\1;/p' $< | { echo '{ global:'; cat; echo 'local: *; };'; } > $@

$(OUT)/libCg.so: $(wildcard $(CGS)/*.cpp $(CGS)/*.h) $(GLDIR)/cg.map $(OUT)/libRSOpenGL.so $(OUT)/shims/compat.o
	$(CXX) -std=c++17 $(GL_CFLAGS) -shared -o $@ $(CGS)/cg_shim.cpp $(CGS)/arb_parse.cpp $(CGS)/arb_glsl.cpp \
	    $(OUT)/shims/compat.o -Wl,--version-script=$(GLDIR)/cg.map -L$(OUT) -lRSOpenGL -Wl,-rpath,'$$ORIGIN' -ldl -pthread

# The Steam library's own tests (shims/steam/tests), as its Makefile runs them on the Mac: the interfaces' vtable
# slots, the DLC folder's listing, and the library's behaviour, with a home and a data folder of their own.
# st_mtimespec is Darwin's name for st_mtim, in one test.
STEAM_TESTS = ../shims/steam/tests
check-steam: $(OUT)/libsteam_api.so
	$(CXX) $(SHIM_CXXFLAGS) -fvisibility=default $(STEAM_TESTS)/test_vtables.cpp $(STEAM)/shim.cpp $(STEAM)/interfaces.cpp \
	    $(OUT)/shims/compat.o -o $(OUT)/shims/test_vtables -ldl -pthread
	$(CXX) $(SHIM_CXXFLAGS) -Dst_mtimespec=st_mtim $(STEAM_TESTS)/test_enumerate.cpp $(OUT)/shims/compat.o \
	    -o $(OUT)/shims/test_enumerate -pthread
	$(CXX) -std=c++17 -O1 $(STEAM_TESTS)/test_shim.cpp -L$(OUT) -lsteam_api -Wl,-rpath,'$$ORIGIN/..' -o $(OUT)/shims/test_shim
	$(OUT)/shims/test_vtables
	$(OUT)/shims/test_enumerate
	@rm -rf $(OUT)/shims/home $(OUT)/shims/data && mkdir -p $(OUT)/shims/home $(OUT)/shims/data
	env -u RS_STEAM_LANGUAGE -u RS_STEAM_ID -u RS_STEAM_PERSONA -u RS_STEAM_LOG -u RS_STEAM_REAL_LIB -u SteamAppId \
	    RS_STEAM_MODE=stub RS_STEAM_DATA_DIR=$(abspath $(OUT))/shims/data HOME=$(abspath $(OUT))/shims/home \
	    RS_STEAM_DLC=248750,258341 RS_STEAM_OWNED=206166,222041 $(OUT)/shims/test_shim

# The renderer's own tests that need no window (shims/opengl/test, shims/cg/tests), as the Mac's ctest runs them:
# an offscreen context drawn into and read back with exact pixels, and the Cg library's hand-written programs on
# both of its backends, every call checked for an OpenGL error.
GL_TEST_FLAGS = -std=gnu11 -O1 -g -Wno-deprecated-declarations -Icompat -include compat/rs_linux.h -I$(GLDIR) -I$(OGL)/src
check-gl: gl
	$(CC) $(GL_TEST_FLAGS) $(OGL)/test/gfx_offscreen_test.c $(OUT)/shims/compat.o -o $(GLDIR)/gfx_offscreen_test \
	    -L$(OUT) -lRSOpenGL -Wl,-rpath,'$$ORIGIN/..' -ldl -lm -pthread
	$(CC) $(GL_TEST_FLAGS) -I$(CGS) ../shims/cg/tests/render_test.c $(OUT)/shims/compat.o -o $(GLDIR)/cg_render_test \
	    -L$(OUT) -lCg -lRSOpenGL -Wl,-rpath,'$$ORIGIN/..' -ldl -lm -pthread
	RS_GL_BACKEND=gl RS_GL_CHECK=1 $(GLDIR)/gfx_offscreen_test
	RS_GL_BACKEND=gl RS_CG_BACKEND=arb $(GLDIR)/cg_render_test
	RS_GL_BACKEND=gl RS_CG_BACKEND=glsl RS_CG_PAIR_CACHE=0 $(GLDIR)/cg_render_test

$(OUT)/layer_test: $(OUT)/test/layer_test.o $(LAYER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# The calls native code makes into the game's code (src/direct.c), on functions of the test's own.
$(OUT)/call_test: test/call_test.c $(OUT)/native/direct.o
	$(CC) -std=gnu11 -Wall -Wextra -O2 -g -o $@ $^ $(LIBS)

$(OUT)/imports_table.inc: darwin-imports.tsv gen_import_table.py import_census.py
	@mkdir -p $(OUT)
	$(PYTHON) gen_import_table.py $@

$(OUT)/src/imports.o: $(OUT)/imports_table.inc

# The game's addresses by name (RC_ADDR_<name>), from the Mac target's tables as every edition's build has them.
$(OUT)/target/rc_target_names.h: $(TARGET)/targets.py $(wildcard $(TARGET)/mac-x64/*) $(wildcard $(TARGET)/names/*)
	@mkdir -p $(OUT)/target
	$(PYTHON) $(TARGET)/targets.py gen --target mac-x64 $(OUT)/target >/dev/null

$(OUT)/src/main.o: $(OUT)/target/rc_target_names.h

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

check: $(OUT)/rs-rearmed $(OUT)/layer_test $(OUT)/call_test check-steam
	$(OUT)/layer_test
	$(OUT)/call_test
	@if [ -f "$(IMAGE)" ]; then $(OUT)/rs-rearmed --image "$(IMAGE)" --init-only; \
	 else echo "no image at $(IMAGE): the game's initializers not run"; fi

# --- the packaged build -------------------------------------------------------------------------------------
#
# `make -C linux appdir`: the program, its libraries and the setup (install/rs_install.py with the Mac's tools it
# uses) as one relocatable folder, laid out as an AppImage's: usr/bin/rs-rearmed is the command (install/launch.sh),
# usr/lib/rs-rearmed the program with its libraries beside it (and the build system's SDL 3: the one library a
# system may lack; PipeWire's, ALSA's and OpenGL's are the system's own), usr/share/rs-rearmed the setup.
# `make -C linux appimage` makes the AppImage of it with appimagetool, downloaded once (install/appimage.sh).
# Nothing of the game is in either: the setup gets the game from the user's own Steam.
APPDIR = $(OUT)/AppDir
APPLIB = $(APPDIR)/usr/lib/rs-rearmed
APPSHARE = $(APPDIR)/usr/share/rs-rearmed
APP_VERSION := $(shell cat ../VERSION)
APP_PROGRAMS = rs-rearmed libsteam_api.so libRSSystemGL.so libRSOpenGL.so libCg.so libBink2.so
SDL_SO := $(shell pkg-config --variable=libdir sdl3 2>/dev/null)/libSDL3.so.0
appdir: all gl bink
	rm -rf $(APPDIR)
	mkdir -p $(APPLIB) $(APPDIR)/usr/bin $(APPSHARE)/tools/play-native $(APPSHARE)/tools/steamless \
	    $(APPSHARE)/tools/targets/mac-x64 $(APPSHARE)/tools/loadscreen $(APPDIR)/usr/share/applications $(APPDIR)/usr/share/icons/hicolor/scalable/apps
	for f in $(APP_PROGRAMS); do strip --strip-debug -o $(APPLIB)/$$f $(OUT)/$$f || exit 1; done
	chmod 755 $(APPLIB)/rs-rearmed
	cp -L $(SDL_SO) $(APPLIB)/
	install -m 755 install/launch.sh $(APPDIR)/usr/bin/rs-rearmed
	install -m 644 install/rs_install.py install/rs_settings.py install/rs_gui.py ../VERSION ../LICENSE \
	    ../THIRD_PARTY_NOTICES.md $(APPSHARE)/
	install -m 644 ../tools/play-native/gamefiles.py ../tools/play-native/cache_overlay.py ../tools/play-native/psarc_key.py \
	    $(APPSHARE)/tools/play-native/
	install -m 644 ../tools/steamless/decrypt.py $(APPSHARE)/tools/steamless/
	install -m 644 ../tools/psarc_extract.py ../tools/psarc_pack.py $(APPSHARE)/tools/
	install -m 644 ../tools/loadscreen/logo.py ../tools/loadscreen/tag.png $(APPSHARE)/tools/loadscreen/
	install -m 644 ../tools/targets/targets.py $(APPSHARE)/tools/targets/
	install -m 644 ../tools/targets/mac-x64/target.toml $(APPSHARE)/tools/targets/mac-x64/
	install -m 644 install/rs-rearmed.desktop $(APPDIR)/usr/share/applications/
	install -m 644 install/rs-rearmed.svg $(APPDIR)/usr/share/icons/hicolor/scalable/apps/
	sed '0,/^Exec=.*/s//&\nX-AppImage-Version=$(APP_VERSION)/' install/rs-rearmed.desktop > $(APPDIR)/rs-rearmed.desktop
	cp install/rs-rearmed.svg $(APPDIR)/rs-rearmed.svg
	ln -s rs-rearmed.svg $(APPDIR)/.DirIcon
	ln -s usr/bin/rs-rearmed $(APPDIR)/AppRun

appimage: appdir
	sh install/appimage.sh $(APPDIR) $(OUT)/RS_Re-ARMed-$(APP_VERSION)-x86_64.AppImage $(OUT)/appimagetool

clean:
	rm -rf $(OUT)/src $(OUT)/darwin $(OUT)/test $(OUT)/target $(OUT)/shell $(OUT)/native $(OUT)/shims $(OUT)/imports_table.inc $(OUT)/rs-rearmed \
	    $(OUT)/layer_test $(OUT)/call_test $(OUT)/libsteam_api.so $(GLDIR) $(OUT)/libRSSystemGL.so $(OUT)/libRSOpenGL.so $(OUT)/libCg.so \
	    $(APPDIR)

.PHONY: all gl bink check check-steam check-gl appdir appimage clean
-include $(OBJ:.o=.d) $(OUT)/test/layer_test.d
