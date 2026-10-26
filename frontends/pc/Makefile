# Builds the PC frontend: snes9x (libretro core, vendored) as a static library, the shared C core and the SDL2 app.
ROOT := $(abspath ../..)
S9X := $(ROOT)/vendor/snes9x_libretro
OUT := $(ROOT)/build/pc
CORE := $(ROOT)/core
CFLAGS := -O2 -I$(CURDIR) -std=gnu11 -w -I$(CORE) -I$(CORE)/gen -I$(CORE)/ui -I$(S9X)/libretro -I$(S9X)/libretro/libretro-common/include $(shell sdl2-config --cflags)
LIBS := $(shell sdl2-config --libs) -lm -lstdc++

GEN := smz3_generate smz3_fill smz3_world smz3_world_gen smz3_rng smz3_patch smz3_rom smz3_text smz3_sprite smz3_hints smz3_itemopts smz3_plando smz3_md5
SRC := $(addprefix $(CORE)/gen/,$(addsuffix .c,$(GEN))) $(CORE)/smz3_state.c $(CORE)/smz3_locations.c $(CORE)/smz3_inventory.c \
       $(CORE)/ui/smz3_ui.c $(CORE)/ui/smz3_ui_opts.c $(CORE)/ui/smz3_run.c $(CORE)/ui/smz3_host.c $(CORE)/ui/smz3_tracker.c \
       $(wildcard pc_*.c) font_data.c $(OUT)/ips_data.c
OBJ := $(patsubst %.c,$(OUT)/o/%.o,$(notdir $(SRC)))

all: $(OUT)/smz3

$(OUT)/libsnes9x.a:
	$(MAKE) -C $(S9X)/libretro platform=unix -j$$(nproc) >/dev/null
	mkdir -p $(OUT)
	cd $(S9X) && ar rcs $(OUT)/libsnes9x.a $$(find . -name '*.o' ! -name 'compat_*snprintf*' | sort)

$(OUT)/ips_data.c: $(ROOT)/tools/gen_ips_c.py
	mkdir -p $(OUT)
	python3 $< $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/ips $@

VPATH := $(sort $(dir $(SRC)))
$(OUT)/o/%.o: %.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/smz3: $(OBJ) $(OUT)/libsnes9x.a
	$(CC) $(OBJ) $(OUT)/libsnes9x.a -o $@ $(LIBS)

clean:
	rm -rf $(OUT)
.PHONY: all clean
