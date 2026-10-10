# Command-line build for the STM32F429 Discovery target.
#
#   make                 debug build   (-O0 -g3), build/debug/rfid_controller.elf
#   make CONFIG=release  release build (-Os -g),  build/release/rfid_controller.elf
#   make flash           build, then program over SWD with STM32_Programmer_CLI
#   make clean
#   EXTRA_CFLAGS=-Werror  extra compiler flags (CI builds with warnings as errors)
#
# Requires arm-none-eabi-gcc on PATH (STM32CubeCLT). The STM32CubeIDE
# project (.cproject) remains usable alongside this file.

CONFIG  ?= debug
TARGET  := rfid_controller
BUILD   := build/$(CONFIG)

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

LDSCRIPT := STM32F429ZITX_FLASH.ld

MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

DEFS := -DUSE_HAL_DRIVER -DSTM32F429xx

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
OPT  := -O0 -g3
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
S_SRCS := $(wildcard Startup/*.s)

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

flash: $(BUILD)/$(TARGET).elf
	STM32_Programmer_CLI -c port=SWD mode=UR -w $< -v -rst

clean:
	rm -rf build

.PHONY: all flash clean

-include $(OBJS:.o=.d)
