DEVKITPRO := /opt/devkitpro
DEVKITARM := $(DEVKITPRO)/devkitARM

CC        := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY   := $(DEVKITARM)/bin/arm-none-eabi-objcopy
GBAFIX    := $(DEVKITARM)/bin/gbafix

SRCS      := main.c
OBJS      := $(SRCS:.c=.o)
ELF       := game.elf
TARGET    := game.gba

CFLAGS    := -mthumb -mthumb-interwork -O2 -Wall -fomit-frame-pointer \
             -ffunction-sections -fdata-sections

LDFLAGS   := -mthumb -mthumb-interwork -specs=gba.specs \
             -Wl,--gc-sections

all: $(TARGET)

$(TARGET): $(ELF)
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -tZOMBIES

$(ELF): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(ELF) $(TARGET)

.PHONY: all clean
