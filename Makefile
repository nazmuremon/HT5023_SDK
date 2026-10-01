# Makefile — HT5023 examples (GCC)
PROJECT := app
BUILD   := build
EXAMPLE ?= blinky
EXAMPLES := blinky led_dance buzzer_music button_press uart uart_ansi ansi_art ansi_art_game boot_banner \
            wdt_monitor timer_poll reset_info uart_ringbuf spi_loopback i2c_scan \
            systick_irq st7789_demo st7789_ui_demo st7789_ui_dma_demo dashboard_demo \
            energy_meter_dashboard touch_ui_demo xpt2046_touch_demo semihosting semihost_rng random_sentence uart_at text_back uart_bridge uart1_selftest uart_probe wisun_get wisun_nr e51_scan usb_echo

SDK_ROOT := sdk
SDK_CORE := $(SDK_ROOT)/cores/ht5023
SDK_BSP  := $(SDK_ROOT)/boards/ht5023_blinky
FLASH_SCRIPT := scripts/run_flash.sh
DETECT_SCRIPT := scripts/run_detect.sh
REGS_SCRIPT   := scripts/run_regs.sh

CC      := arm-none-eabi-gcc
AS      := arm-none-eabi-gcc
LD      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump

SPEED ?= 1000
FLASH_ADDR ?= 0x00000000
ERASE_MODE ?= mass
RUN_AFTER_FLASH ?= 1
BIN_PATH ?= $(BUILD)/$(PROJECT).bin

ifneq ($(filter $(EXAMPLE),$(EXAMPLES)),$(EXAMPLE))
$(error Unknown EXAMPLE '$(EXAMPLE)'. Use one of: $(EXAMPLES))
endif

CFLAGS  := -mcpu=cortex-m0 -mthumb -Os -g -ffunction-sections -fdata-sections -fno-builtin \
           -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

# Extra defines from the command line. CFLAGS is assigned with := so the
# environment cannot reach it; this is the hook for one-off builds, e.g.
#   make bootloader EXTRA_CFLAGS=-DHT5023_DISABLE_WDT_BY_DEFAULT=0
CFLAGS += $(EXTRA_CFLAGS)

# Header dependency tracking. Without it, editing a header -- such as an
# example's configuration file -- rebuilds nothing and the old objects are
# silently relinked.
DEPFLAGS := -MMD -MP

# Rebuild when the compiler flags change, not just the sources: otherwise
# `make EXTRA_CFLAGS=-DHT5023_FSYS_HZ=...` over an existing build silently
# relinks objects compiled with the old value. The file is rewritten only when
# the flags differ, so its timestamp is what the objects depend on.
FLAGS_FILE := $(BUILD)/.cflags
FLAGS_SIG  := $(CFLAGS) $(INCS)
ifneq ($(file <$(FLAGS_FILE)),$(FLAGS_SIG))
$(shell mkdir -p $(BUILD))
$(file >$(FLAGS_FILE),$(FLAGS_SIG))
endif

ASFLAGS := -mcpu=cortex-m0 -mthumb
# BOOT_APP=1 links the application to run under the UART bootloader (at 0x2000)
# instead of standing alone at 0x0000.
BOOT_APP ?= 0
ifeq ($(BOOT_APP),1)
APP_LD := $(SDK_CORE)/ld/ht5023_app.ld
else
APP_LD := $(SDK_CORE)/ld/ht5023.ld
endif
LDFLAGS := -T $(APP_LD) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(PROJECT).map

COMMON_SRCS := $(SDK_CORE)/src/startup_ht5023.s \
               $(SDK_CORE)/src/system_ht5023.c \
               $(SDK_BSP)/src/board_blinky_gpio.c \
               $(SDK_BSP)/src/board_buzzer.c
LIB_SRCS    := lib/ht5023_uart/src/ht5023_uart.c \
               lib/ht5023_log/src/ht5023_log.c \
               lib/ht5023_debug/src/ht5023_debug.c \
               lib/ht5023_boot_banner/src/ht5023_boot_banner.c \
               lib/ht5023_dma/src/ht5023_dma.c \
               lib/ht5023_wdt/src/ht5023_wdt.c \
               lib/ht5023_timer/src/ht5023_timer.c \
               lib/ht5023_reset/src/ht5023_reset.c \
               lib/ht5023_ringbuf/src/ht5023_ringbuf.c \
               lib/ht5023_spi/src/ht5023_spi.c \
               lib/ht5023_i2c/src/ht5023_i2c.c \
               lib/ht5023_systick/src/ht5023_systick.c \
               lib/ht5023_buzzer/src/ht5023_buzzer.c \
               lib/ht5023_st7789/src/ht5023_st7789.c \
               lib/ht5023_gfx/src/ht5023_gfx.c \
               lib/ht5023_font5x7/src/ht5023_font5x7.c \
               lib/ht5023_text/src/ht5023_text.c \
               lib/ht5023_fb/src/ht5023_fb.c \
               lib/ht5023_xpt2046/src/ht5023_xpt2046.c
# Every .c directly inside the example folder (not its test/ subfolder), so an
# example can be split into modules. Single-file examples are unaffected.
APP_SRCS    := $(sort $(wildcard examples/$(EXAMPLE)/*.c))
SRCS := $(COMMON_SRCS) $(LIB_SRCS) $(APP_SRCS)
INCS := -I$(SDK_ROOT)/include -I$(SDK_CORE)/include -I$(SDK_BSP)/include \
        -Isrc \
        -Ilib/ht5023_uart/include -Ilib/ht5023_log/include \
        -Ilib/ht5023_debug/include -Ilib/ht5023_boot_banner/include \
        -Ilib/ht5023_dma/include \
        -Ilib/ht5023_wdt/include -Ilib/ht5023_timer/include \
        -Ilib/ht5023_reset/include -Ilib/ht5023_ringbuf/include \
        -Ilib/ht5023_spi/include -Ilib/ht5023_i2c/include -Ilib/ht5023_systick/include \
        -Ilib/ht5023_buzzer/include -Ilib/ht5023_st7789/include \
        -Ilib/ht5023_gfx/include -Ilib/ht5023_font5x7/include \
        -Ilib/ht5023_text/include -Ilib/ht5023_fb/include -Ilib/ht5023_xpt2046/include

OBJS := $(addprefix $(BUILD)/, $(SRCS:.c=.o))
OBJS := $(OBJS:.s=.o)

# Every example links to the same $(BUILD)/$(PROJECT).elf, so switching EXAMPLE
# has to force a relink: without this, going back to an example whose main.o is
# older than the current .elf leaves make thinking the binary is up to date and
# the previous example gets flashed instead.
EXAMPLE_TAG := $(BUILD)/.example-$(EXAMPLE)

.PHONY: all build bootloader flash detect regs clean help list-examples

all: build

build: $(BUILD)/$(PROJECT).bin

$(BUILD)/%.o: %.c $(FLAGS_FILE)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCS) -c $< -o $@

$(BUILD)/%.o: %.s $(FLAGS_FILE)
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(EXAMPLE_TAG):
	mkdir -p $(BUILD)
	rm -f $(BUILD)/.example-*
	touch $@

$(BUILD)/$(PROJECT).elf: $(OBJS) $(EXAMPLE_TAG)
	$(LD) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@
	$(OBJDUMP) -d $< > $(BUILD)/$(PROJECT).lst
	@echo "Built: $@"
	@echo "Example: $(EXAMPLE)"

# --- UART bootloader ------------------------------------------------------
BOOT_SRCS := bootloader/startup_boot.s bootloader/main.c              $(SDK_CORE)/src/system_ht5023.c lib/ht5023_uart/src/ht5023_uart.c
BOOT_OBJS := $(addprefix $(BUILD)/boot/, $(BOOT_SRCS:.c=.o))
BOOT_OBJS := $(BOOT_OBJS:.s=.o)

$(BUILD)/boot/%.o: %.c $(FLAGS_FILE)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCS) -c $< -o $@

$(BUILD)/boot/%.o: %.s $(FLAGS_FILE)
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

bootloader: $(BUILD)/boot.bin

$(BUILD)/boot.elf: $(BOOT_OBJS)
	$(LD) $(CFLAGS) $(BOOT_OBJS) -o $@ -T bootloader/boot.ld -nostartfiles 	      -Wl,--gc-sections -Wl,-Map=$(BUILD)/boot.map

$(BUILD)/boot.bin: $(BUILD)/boot.elf
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@
	$(OBJDUMP) -d $< > $(BUILD)/boot.lst
	@echo "Built bootloader: $@"

flash:
	SPEED="$(SPEED)" FLASH_ADDR="$(FLASH_ADDR)" ERASE_MODE="$(ERASE_MODE)" \
	RUN_AFTER_FLASH="$(RUN_AFTER_FLASH)" BIN_PATH="$(BIN_PATH)" bash $(FLASH_SCRIPT)

detect:
	SPEED="$(SPEED)" bash $(DETECT_SCRIPT)

regs:
	SPEED="$(SPEED)" bash $(REGS_SCRIPT)

-include $(OBJS:.o=.d) $(BOOT_OBJS:.o=.d)

list-examples:
	@printf '%s\n' $(EXAMPLES)

help:
	@echo "Targets:"
	@echo "  make build EXAMPLE=<name>"
	@echo "  make clean"
	@echo "  make flash [EXAMPLE=<name>] [SPEED=1000] [ERASE_MODE=mass|pages] [FLASH_ADDR=0x00000000] [RUN_AFTER_FLASH=1]"
	@echo "  make detect [SPEED=1000]"
	@echo "  make regs [SPEED=1000]"
	@echo "  make list-examples"
	@echo "Examples:"
	@printf '  %s\n' $(EXAMPLES)

clean:
	rm -rf $(BUILD)
