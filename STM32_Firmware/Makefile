# Command-line build for the reader firmware.
#
#   make                 debug build   (-O0 -g3), build/debug/rfid_controller.elf
#   make CONFIG=release  release build (-Os -g),  build/release/rfid_controller.elf
#   make BOARD=f411      F411 custom board (default BOARD=f429, the Discovery);
#                        output build/f411-<config>/rfid_controller_f411.elf.
#                        F411 debug builds use -Og: at -O0 the image overflows
#                        the 128 KB code area.
#   make flash           build, check that the chip on SWD matches BOARD, then
#                        program the bootloader and the application with
#                        STM32_Programmer_CLI
#   make flash-app       only the application (the board already has the
#                        bootloader in sector 0)
#   make bootloader      only the bootloader, build/boot-<board>/bootloader.elf
#   make image           signed update image for Tools/fw_update.py,
#                        build/<...>/<target>-<VERSION>.tlfw; needs the
#                        signing key (SIGN_KEY, default keys/fw_signing_key.pem)
#   make check-chip      only the chip check (reads the device ID, no reset)
#   make protect         production: set the brown-out level and read
#                        protection level 1 (flash can no longer be read
#                        or programmed over SWD). Run after make flash.
#   make unprotect       back to level 0. MASS-ERASES the chip, firmware
#                        and saved settings included.
#   make hostcheck       build and run the host-side protocol test with the
#                        PC compiler (HOSTCC, default gcc)
#   make clean
#   EXTRA_CFLAGS=-Werror  extra compiler flags (CI builds with warnings as errors)
#
# Requires arm-none-eabi-gcc on PATH (STM32CubeCLT). The STM32CubeIDE
# project (.cproject) remains usable alongside this file.

CONFIG  ?= debug
BOARD   ?= f429
PROGRAMMER ?= STM32_Programmer_CLI
HOSTCC  ?= gcc
# Explicit suffix prevents stale extensionless Windows test binaries from running.
ifeq ($(OS),Windows_NT)
HOST_EXE := .exe
else
HOST_EXE :=
endif

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

ifeq ($(BOARD),f429)
TARGET   := rfid_controller
CHIP_ID  := 0x419
BUILD    := build/$(CONFIG)
LDSCRIPT := STM32F429ZITX_FLASH.ld
BOOT_LDSCRIPT := Bootloader/bootloader_f429.ld
S_SRCS   := $(wildcard Startup/*.s)
DEFS     := -DUSE_HAL_DRIVER -DSTM32F429xx
DEBUG_OPT := -O0 -g3
BOR_LEV  := 0x1
else ifeq ($(BOARD),f411)
# Chip files from the STM32CubeMX project (Boards/f411/F411_custom.ioc).
# They live outside Startup/ so the F429 CubeIDE project never builds them.
TARGET   := rfid_controller_f411
CHIP_ID  := 0x431
BUILD    := build/f411-$(CONFIG)
LDSCRIPT := Boards/f411/STM32F411CEUX_FLASH.ld
BOOT_LDSCRIPT := Bootloader/bootloader_f411.ld
S_SRCS   := Boards/f411/startup_stm32f411ceux.s
DEFS     := -DUSE_HAL_DRIVER -DSTM32F411xE -DTARGET_BOARD=2
DEBUG_OPT := -Og -g3
BOR_LEV  := 0x0
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

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin bootloader

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

# ---------------------------------------------------------------------
# Bootloader (flash sector 0). Always size-optimised, no HAL, soft
# float: it never enables the FPU. micro-ecc builds only ECDSA
# verification on P-256.
# ---------------------------------------------------------------------
BOOT_BUILD := build/boot-$(BOARD)
BOOT_DEFS  := $(filter -DSTM32% -DTARGET_BOARD%,$(DEFS)) \
	-DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
	-DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 \
	-DuECC_SUPPORT_COMPRESSED_POINT=0 -DuECC_OPTIMIZATION_LEVEL=1
BOOT_CFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft $(BOOT_DEFS) \
	-IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include \
	-Os -g -std=gnu11 -Wall -ffunction-sections -fdata-sections -MMD -MP \
	$(EXTRA_CFLAGS)
BOOT_SRCS  := $(wildcard Bootloader/*.c) Bootloader/micro-ecc/uECC.c \
	Firmware/Common/sha256.c Firmware/Common/firmware_image.c
BOOT_OBJS  := $(addprefix $(BOOT_BUILD)/,$(BOOT_SRCS:.c=.o))

bootloader: $(BOOT_BUILD)/bootloader.elf $(BOOT_BUILD)/bootloader.hex

$(BOOT_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_BUILD)/bootloader.elf: $(BOOT_OBJS) $(BOOT_LDSCRIPT) Bootloader/bootloader_sections.ld
	$(CC) -mcpu=cortex-m4 -mthumb -mfloat-abi=soft $(BOOT_OBJS) -nostartfiles \
		-LBootloader -T$(BOOT_LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
		-Wl,--gc-sections -Wl,-Map=$(BOOT_BUILD)/bootloader.map \
		-Wl,--print-memory-usage -o $@
	$(SIZE) $@

$(BOOT_BUILD)/bootloader.hex: $(BOOT_BUILD)/bootloader.elf
	$(OBJCOPY) -O ihex $< $@

# Signed update image. VERSION defaults to the git tag description.
SIGN_KEY ?= keys/fw_signing_key.pem
VERSION  ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)
PYTHON   ?= python

image: $(BUILD)/$(TARGET).bin
	$(PYTHON) Tools/fw_sign.py sign --key $(SIGN_KEY) --board $(BOARD) \
		--version "$(VERSION)" $< $(BUILD)/$(TARGET)-$(VERSION).tlfw

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

flash: $(BUILD)/$(TARGET).elf $(BOOT_BUILD)/bootloader.elf check-chip
	$(PROGRAMMER) -c port=SWD mode=UR -w $(BOOT_BUILD)/bootloader.elf -v \
		-w $(BUILD)/$(TARGET).elf -v -rst

flash-app: $(BUILD)/$(TARGET).elf check-chip
	$(PROGRAMMER) -c port=SWD mode=UR -w $< -v -rst

# Read protection level 1 for shipped readers. The firmware leaves the
# option bytes alone once protection is set (brownout.c), so the
# brown-out level (BOR_LEV register value: 0x0 = level 3, 0x1 = level 2,
# matching brownout.c) is written in the same step. Level 2 is never
# used: it is permanent and also locks out every later recovery.
protect: check-chip
	$(PROGRAMMER) -c port=SWD mode=UR -ob BOR_LEV=$(BOR_LEV) RDP=0xBB
	$(PROGRAMMER) -c port=SWD mode=HOTPLUG -ob displ | grep -E "RDP|BOR_LEV"

unprotect: check-chip
	@echo "unprotect: this mass-erases the chip (firmware and saved settings)."
	$(PROGRAMMER) -c port=SWD mode=UR -ob RDP=0xAA

clean:
	rm -rf build

# Host-side tests of the Identium protocol framing and tag queue, of
# the tag backup log on a simulated SPI flash, of the bootloader's image
# check against images signed by Tools/fw_sign.py (needs Python with
# the cryptography package), and of the update receiver. Stub device
# headers (hostcheck/stubs) replace the ARM CMSIS headers.
hostcheck:
	@mkdir -p build/hostcheck
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror -DSTM32F411xE -DTARGET_BOARD=2 -Ihostcheck/stubs hostcheck/io_connection_host_test.c -o build/hostcheck/io_connection_host_test$(HOST_EXE)
	build/hostcheck/io_connection_host_test$(HOST_EXE)
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror -DSTM32F411xE -Ihostcheck/stubs hostcheck/http_retry_host_test.c -o build/hostcheck/http_retry_host_test$(HOST_EXE)
	build/hostcheck/http_retry_host_test$(HOST_EXE)
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror -DSTM32F411xE -Ihostcheck/stubs hostcheck/configuration_storage_host_test.c -o build/hostcheck/configuration_storage_host_test$(HOST_EXE)
	build/hostcheck/configuration_storage_host_test$(HOST_EXE)
	$(HOSTCC) -std=gnu11 -Wall -Werror -DSTM32F429xx -Ihostcheck/stubs 		hostcheck/identium_protocol_host_test.c Firmware/Protocol/identium_protocol.c 		-o build/hostcheck/identium_protocol_host_test$(HOST_EXE)
	build/hostcheck/identium_protocol_host_test$(HOST_EXE)
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror -DSTM32F411xE -DTARGET_BOARD=2 -Ihostcheck/stubs \
		hostcheck/tag_log_host_test.c Firmware/Services/storage/tag_log.c \
		-o build/hostcheck/tag_log_host_test$(HOST_EXE)
	build/hostcheck/tag_log_host_test$(HOST_EXE)
	$(PYTHON) hostcheck/make_test_images.py build/hostcheck
	for b in F429ZI:STM32F429xx:f429 F411CE:STM32F411xE:f411; do \
		mcu=$${b#*:}; mcu=$${mcu%%:*}; board=$${b##*:}; \
		$(HOSTCC) -std=gnu11 -Wall -Werror -Wno-unknown-pragmas -D$$mcu $(filter -DuECC%,$(BOOT_DEFS)) \
			hostcheck/boot_image_host_test.c Bootloader/boot_image.c \
			Bootloader/micro-ecc/uECC.c Firmware/Common/firmware_image.c \
			Firmware/Common/sha256.c build/hostcheck/test_public_key.c \
			-o build/hostcheck/boot_image_host_test_$$board$(HOST_EXE) || exit 1; \
		build/hostcheck/boot_image_host_test_$$board$(HOST_EXE) \
			build/hostcheck/test_$$board.tlfw || exit 1; \
	done
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror -Wno-unknown-pragmas -DSTM32F411xE -DTARGET_BOARD=2 \
		$(filter -DuECC%,$(BOOT_DEFS)) -Ihostcheck/stubs -include hostcheck/stubs/fw_update_host.h \
		hostcheck/firmware_update_host_test.c Firmware/Services/update/firmware_update.c \
		Bootloader/boot_image.c Bootloader/micro-ecc/uECC.c Firmware/Common/firmware_image.c \
		Firmware/Common/sha256.c build/hostcheck/test_public_key.c \
		-o build/hostcheck/firmware_update_host_test$(HOST_EXE)
	build/hostcheck/firmware_update_host_test$(HOST_EXE) build/hostcheck/test_f411.tlfw \
		build/hostcheck/test_f429.tlfw

.PHONY: all bootloader image flash flash-app check-chip protect unprotect hostcheck clean

-include $(OBJS:.o=.d) $(BOOT_OBJS:.o=.d)
