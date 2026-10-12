# Command-line build for the reader firmware.
#
#   make                 debug build   (-O0 -g3), build/debug/rfid_controller.elf
#   make CONFIG=release  release build (-Os -g),  build/release/rfid_controller.elf
#   make BOARD=f411      F411 custom board (default BOARD=f429, the Discovery);
#                        output build/f411-<config>/rfid_controller_f411.elf.
#                        F411 debug builds use -Og: at -O0 the image overflows
#                        the 128 KB code area.
#   make flash           build, check that the chip on SWD matches BOARD, then
#                        program it with STM32_Programmer_CLI
#   make check-chip      only the chip check (reads the device ID, no reset)
#   make clean
#   EXTRA_CFLAGS=-Werror  extra compiler flags (CI builds with warnings as errors)
#
# Requires arm-none-eabi-gcc on PATH (STM32CubeCLT). The STM32CubeIDE
# project (.cproject) remains usable alongside this file.

CONFIG  ?= debug
BOARD   ?= f429
PROGRAMMER ?= STM32_Programmer_CLI

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

ifeq ($(BOARD),f429)
TARGET   := rfid_controller
CHIP_ID  := 0x419
BUILD    := build/$(CONFIG)
LDSCRIPT := STM32F429ZITX_FLASH.ld
S_SRCS   := $(wildcard Startup/*.s)
DEFS     := -DUSE_HAL_DRIVER -DSTM32F429xx
DEBUG_OPT := -O0 -g3
else ifeq ($(BOARD),f411)
# Chip files from the STM32CubeMX project (Boards/f411/F411_custom.ioc).
# They live outside Startup/ so the F429 CubeIDE project never builds them.
TARGET   := rfid_controller_f411
CHIP_ID  := 0x431
BUILD    := build/f411-$(CONFIG)
LDSCRIPT := Boards/f411/STM32F411CEUX_FLASH.ld
S_SRCS   := Boards/f411/startup_stm32f411ceux.s
DEFS     := -DUSE_HAL_DRIVER -DSTM32F411xE -DTARGET_BOARD=2
DEBUG_OPT := -Og -g3
else
$(error BOARD must be f429 or f411)
endif

MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard


INCS := \
	-IInc \
	-IDrivers/STM32F4xx_HAL_Driver/Inc \
	-IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
	-IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
	-IDrivers/CMSIS/Include \
	-IFirmware/USB_DEVICE/Target \
	-IFirmware/Services/host \
	-IFirmware/Services/USB \
	-IFirmware/Middlewares/ST/STM32_USB_Device_Library/Core/Inc \
	-IFirmware/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
	-IFirmware/Drivers/uart \
	-IFirmware/Drivers/spi \
	-IFirmware/Drivers/rcc \
	-IFirmware/Drivers/gpio \
	-IFirmware/Application/Silion \
	-IFirmware/Application/Run_time

ifeq ($(CONFIG),release)
OPT := -Os -g
else ifeq ($(CONFIG),debug)
OPT  := $(DEBUG_OPT)
DEFS += -DDEBUG
else
$(error CONFIG must be debug or release)
endif

CFLAGS := $(MCU) $(DEFS) $(INCS) $(OPT) -std=gnu11 -Wall \
	-ffunction-sections -fdata-sections -fstack-usage -MMD -MP 	$(EXTRA_CFLAGS)

LDFLAGS := $(MCU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
	-Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
	-Wl,--print-memory-usage -static

C_SRCS := $(shell find Src Drivers Firmware -name '*.c')

OBJS := $(addprefix $(BUILD)/,$(C_SRCS:.c=.o) $(S_SRCS:.s=.o))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -x assembler-with-cpp -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Refuse to flash an image built for the other board: read the device ID
# over SWD (hot-plug, the target keeps running) and compare it with BOARD.
# 0x419 = STM32F42x/F43x (Discovery), 0x431 = STM32F411 (custom board).
check-chip:
	@id=$$($(PROGRAMMER) -c port=SWD mode=HOTPLUG 2>&1 | sed -n 's/^Device ID *: *\(0x[0-9A-Fa-f]*\).*/\1/p' | head -n 1); \
	if [ -z "$$id" ]; then \
		echo "check-chip: no STM32 found over SWD" >&2; exit 1; \
	fi; \
	if [ "$$id" != "$(CHIP_ID)" ]; then \
		echo "check-chip: the connected chip is $$id, but this is a BOARD=$(BOARD) build (expects $(CHIP_ID)). Not flashing." >&2; exit 1; \
	fi; \
	echo "check-chip: $$id matches BOARD=$(BOARD)"

flash: $(BUILD)/$(TARGET).elf check-chip
	$(PROGRAMMER) -c port=SWD mode=UR -w $< -v -rst

clean:
	rm -rf build

.PHONY: all flash check-chip clean

-include $(OBJS:.o=.d)
