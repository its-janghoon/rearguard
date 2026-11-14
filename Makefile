# Build everything for QNX 8.0 x86_64 (QEMU). Needs: source ~/qnx800/qnxsdp-env.sh
CC      = qcc -Vgcc_ntox86_64
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -D_QNX_SOURCE
OUT     = build/x86_64
LVGL    = third_party/lvgl
UIINC   = -Isrc/ui -I$(LVGL) -DLV_CONF_INCLUDE_SIMPLE

LVGL_SRC := $(shell find $(LVGL)/src -name '*.c')
LVGL_OBJ := $(patsubst %.c,$(OUT)/obj/%.o,$(LVGL_SRC))
FONT_SRC := $(wildcard src/ui/fonts/*.c)
FONT_OBJ := $(patsubst %.c,$(OUT)/obj/%.o,$(FONT_SRC))
COMMON   := $(OUT)/obj/src/common/common.o

BINS = vbusd vsim camsim rvc cluster infotainment supervisor copilot otad secc evcc probe
all: $(addprefix $(OUT)/,$(BINS))

$(OUT)/obj/$(LVGL)/%.o: $(LVGL)/%.c src/ui/lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) -O2 -D_QNX_SOURCE $(UIINC) -c $< -o $@

$(OUT)/obj/src/%.o: src/%.c $(wildcard src/common/*.h src/ui/*.h src/rvc/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(UIINC) -c $< -o $@

$(OUT)/liblvgl.a: $(LVGL_OBJ) $(FONT_OBJ)
	@rm -f $@; $(CC) -A $@ $^ 2>/dev/null || ntox86_64-ar rcs $@ $^

UI = $(OUT)/obj/src/ui/lvscreen.o $(OUT)/obj/src/ui/widgets.o $(OUT)/liblvgl.a

$(OUT)/vbusd: $(OUT)/obj/src/vbusd/vbusd.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/vsim: $(OUT)/obj/src/vsim/vsim.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/camsim: $(OUT)/obj/src/camsim/camsim.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/rvc: $(OUT)/obj/src/rvc/rvc.o $(COMMON)
	$(CC) -o $@ $^ -lscreen
$(OUT)/obj/src/rvc/rvc_embed.o: src/rvc/rvc.c src/rvc/rvc.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DRVC_EMBED -c $< -o $@
$(OUT)/cluster: $(OUT)/obj/src/cluster/cluster.o $(UI) $(COMMON)
	$(CC) -o $@ $^ -lscreen -lm
$(OUT)/infotainment: $(OUT)/obj/src/infotainment/infotainment.o $(OUT)/obj/src/rvc/rvc_embed.o $(UI) $(COMMON)
	$(CC) -o $@ $^ -lscreen -lm
$(OUT)/supervisor: $(OUT)/obj/src/supervisor/supervisor.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/copilot: $(OUT)/obj/src/copilot/copilot.o $(COMMON)
	$(CC) -o $@ $^ -lm
$(OUT)/otad: $(OUT)/obj/src/otad/otad.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/secc: $(OUT)/obj/src/charge/secc.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/evcc: $(OUT)/obj/src/charge/evcc.o $(COMMON)
	$(CC) -o $@ $^
$(OUT)/probe: $(OUT)/obj/src/probe/probe.o $(COMMON)
	$(CC) -o $@ $^ -lscreen

clean:
	rm -rf build

.PHONY: all clean

assets/cam.bgra: assets/cam.jpg
	magick $< -resize 1280x720! -depth 8 BGRA:$@
all: assets/cam.bgra
