# Build the data logger with arm-none-eabi-gcc.
#   make              debug commands included
#   make RELEASE=1    debug commands left out (CLI_DEBUG_CMDS = 0)
#   make flash        write build/datalogger.bin with st-flash (ST-Link)
#   make clean

TARGET  = datalogger
BUILD   = build
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

SRCS    = $(wildcard src/*.c) fatfs/ff.c startup/startup_stm32f103.c \
          $(wildcard freertos/*.c)
OBJS    = $(SRCS:%.c=$(BUILD)/%.o)
LDSCRIPT = startup/stm32f103c8.ld

CFLAGS  = -mcpu=cortex-m3 -mthumb -std=c99 -Wall -Wextra -Os -g \
          -ffunction-sections -fdata-sections -Isrc -Ifatfs -Ifreertos/include -MMD -MP
ifeq ($(RELEASE),1)
CFLAGS += -DCLI_DEBUG_CMDS=0
endif
LDFLAGS = -mcpu=cortex-m3 -mthumb -T$(LDSCRIPT) -nostartfiles \
          --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
          -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--no-warn-rwx-segments

all: $(BUILD)/$(TARGET).bin
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(BUILD)/$(TARGET).bin
	st-flash --reset write $< 0x08000000

clean:
	rm -rf $(BUILD)

.PHONY: all flash clean
-include $(OBJS:.o=.d)
