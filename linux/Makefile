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
    -fno-strict-aliasing -pthread $(CFLAGS) -Isrc -Icompat -include compat/rs_linux.h -I$(OUT)/target -I$(RUNTIME) \
    -I$(OVERRIDES) -I$(OVERRIDES)/gen -I$(GAME)/include -I../shims/include
NATIVE_OBJ = $(OUT)/native/direct.o $(OUT)/native/patch.o $(OUT)/native/online_glue.o $(OUT)/native/online.o \
    $(OUT)/native/online_stub.o
$(OUT)/target/overrides_online.inc: $(TARGET)/names/overrides/online.list
	@mkdir -p $(OUT)/target
	sed -n 's/^\([a-z][a-z0-9_]*\).*/OV(\1)/p' $< > $@
$(OUT)/native/direct.o: src/direct.c $(OUT)/target/rc_target_names.h
	@mkdir -p $(OUT)/native
	$(CC) $(NATIVE_CFLAGS) -c -o $@ $<
$(OUT)/native/patch.o: src/patch.c src/rsl.h $(OUT)/target/rc_target_names.h $(OUT)/target/overrides_online.inc
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
	    -Isrc -Idarwin -c -o $@ $<
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

$(OUT)/rs-rearmed: $(OBJ) $(NATIVE_OBJ) $(SHELL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(NATIVE_OBJ) $(SHELL_OBJ) $(LIBS) $(SDL_LIBS) $(PW_LIBS)

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

check: $(OUT)/rs-rearmed $(OUT)/layer_test check-steam
	$(OUT)/layer_test
	@if [ -f "$(IMAGE)" ]; then $(OUT)/rs-rearmed --image "$(IMAGE)" --init-only; \
	 else echo "no image at $(IMAGE): the game's initializers not run"; fi

clean:
	rm -rf $(OUT)/src $(OUT)/darwin $(OUT)/test $(OUT)/target $(OUT)/shell $(OUT)/native $(OUT)/shims $(OUT)/imports_table.inc $(OUT)/rs-rearmed \
	    $(OUT)/layer_test $(OUT)/libsteam_api.so $(GLDIR) $(OUT)/libRSSystemGL.so $(OUT)/libRSOpenGL.so $(OUT)/libCg.so

.PHONY: all gl check check-steam check-gl clean
-include $(OBJ:.o=.d) $(OUT)/test/layer_test.d
