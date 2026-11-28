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
ALL_CFLAGS = -std=gnu11 -Wall -Wextra -fno-strict-aliasing -pthread $(CFLAGS) -Isrc -Idarwin -I$(OUT) -I$(OUT)/target
LIBS = -ldl -lm -pthread

LAYER = $(sort $(wildcard darwin/*.c))
SRC = src/main.c src/loader.c src/imports.c $(LAYER)
OBJ = $(SRC:%.c=$(OUT)/%.o)
LAYER_OBJ = $(LAYER:%.c=$(OUT)/%.o)

all: $(OUT)/rs-rearmed $(OUT)/libsteam_api.so

$(OUT)/rs-rearmed: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

# The project's replacement libraries, from the sources the Mac app builds them from (shims/), each a shared
# library beside the program. compat/ has the few macOS functions they call.
SHIM_CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -fno-exceptions -Icompat
STEAM = ../shims/steam/src
$(OUT)/libsteam_api.so: $(STEAM)/shim.cpp $(STEAM)/interfaces.cpp $(STEAM)/shim.h $(STEAM)/enumerate.h compat/compat.c $(wildcard compat/*.h compat/*/*.h)
	@mkdir -p $(OUT)/shims
	$(CC) -O2 -g -Wall -Wextra -fPIC -fvisibility=hidden -Icompat -c -o $(OUT)/shims/compat.o compat/compat.c
	$(CXX) $(SHIM_CXXFLAGS) -shared -o $@ $(STEAM)/shim.cpp $(STEAM)/interfaces.cpp $(OUT)/shims/compat.o -ldl -pthread

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

$(OUT)/layer_test: $(OUT)/test/layer_test.o $(LAYER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OUT)/imports_table.inc: darwin-imports.tsv gen_import_table.py import_census.py
	@mkdir -p $(OUT)
	$(PYTHON) gen_import_table.py $@

$(OUT)/src/imports.o: $(OUT)/imports_table.inc

# The game's addresses by name (RC_ADDR_<name>), from the Mac target's tables as every edition's build has them.
TARGET = ../tools/targets
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
	rm -rf $(OUT)/src $(OUT)/darwin $(OUT)/test $(OUT)/target $(OUT)/shims $(OUT)/imports_table.inc $(OUT)/rs-rearmed \
	    $(OUT)/layer_test $(OUT)/libsteam_api.so

.PHONY: all check check-steam clean
-include $(OBJ:.o=.d) $(OUT)/test/layer_test.d
