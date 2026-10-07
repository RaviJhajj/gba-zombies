# Define the toolchain and paths
DEVKITPRO := /opt/devkitpro
DEVKITARM := $(DEVKITPRO)/devkitARM
LIBGBA    := $(DEVKITPRO)/libgba

CC        := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY   := $(DEVKITARM)/bin/arm-none-eabi-objcopy
GBAFIX    := $(DEVKITARM)/bin/gbafix

# Source and Target names
SRCS      := main.c
OBJS      := $(SRCS:.c=.o)
ELF       := game.elf
TARGET    := game.gba

# Compiler flags
# Includes libgba headers and sets standard GBA architecture flags
CFLAGS    := -mthumb -mthumb-interwork -O2 -Wall -fomit-frame-pointer \
             -ffunction-sections -fdata-sections \
             -I$(LIBGBA)/include

# Linker flags
# Uses standard GBA spec, garbage-collects unused sections, links libgba
LDFLAGS   := -mthumb -mthumb-interwork -specs=gba.specs \
             -Wl,--gc-sections \
             -L$(LIBGBA)/lib -lgba

all: $(TARGET)

$(TARGET): $(ELF)
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -tZOMBIES

$(ELF): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(ELF) $(TARGET) output.map

.PHONY: all clean

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
