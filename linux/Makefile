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
ALL_CFLAGS = -std=gnu11 -Wall -Wextra -fno-strict-aliasing -pthread $(CFLAGS) -Isrc -Idarwin -I$(OUT)
LIBS = -ldl -lm -pthread

LAYER = $(sort $(wildcard darwin/*.c))
SRC = src/main.c src/loader.c src/imports.c $(LAYER)
OBJ = $(SRC:%.c=$(OUT)/%.o)
LAYER_OBJ = $(LAYER:%.c=$(OUT)/%.o)

all: $(OUT)/rs-rearmed

$(OUT)/rs-rearmed: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OUT)/layer_test: $(OUT)/test/layer_test.o $(LAYER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OUT)/imports_table.inc: darwin-imports.tsv gen_import_table.py import_census.py
	@mkdir -p $(OUT)
	$(PYTHON) gen_import_table.py $@

$(OUT)/src/imports.o: $(OUT)/imports_table.inc

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

check: $(OUT)/rs-rearmed $(OUT)/layer_test
	$(OUT)/layer_test
	@if [ -f "$(IMAGE)" ]; then $(OUT)/rs-rearmed --image "$(IMAGE)" --init-only; \
	 else echo "no image at $(IMAGE): the game's initializers not run"; fi

clean:
	rm -rf $(OUT)/src $(OUT)/darwin $(OUT)/test $(OUT)/imports_table.inc $(OUT)/rs-rearmed $(OUT)/layer_test

.PHONY: all check clean
-include $(OBJ:.o=.d) $(OUT)/test/layer_test.d
