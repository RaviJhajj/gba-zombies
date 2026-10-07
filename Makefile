# Define the toolchain and paths
DEVKITARM := /opt/devkitpro/devkitARM
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
CXX := $(DEVKITARM)/bin/arm-none-eabi-g++
AR := $(DEVKITARM)/bin/arm-none-eabi-ar
OBJCOPY := $(DEVKITARM)/bin/arm-none-eabi-objcopy

# Define the source and object files
SRCS := main.c
OBJS := $(SRCS:.c=.o)

# Define the output ROM file
TARGET := game.gba

# Compiler flags
CFLAGS := -Wall -O2 -g -mthumb -mtune=arm7tdmi -mno-thumb-interwork -fomit-frame-pointer -funroll-loops -ffunction-sections -fdata-sections -Wl,-Map=output.map -Tgba_flash.ld

# Linker flags
LDFLAGS := -specs=gba.specs -nostartfiles -nodefaultlibs -lgcc

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
