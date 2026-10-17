# ps5-signal: build with ./build.sh (the payload SDK and the container), not make directly.
#   make           ps5-signal.elf      listens on 127.0.0.1 only
#   make dev       ps5-signal-dev.elf  also listens on the network, for testing from a PC; never release it
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS  += -std=c11 -Wall -Wextra -O2 -fno-stack-protector
LDLIBS  += -lSceUserService -lpthread
SOURCES := src/main.c src/config.c src/icc.c src/indicator.c src/patterns.c src/player.c src/http.c src/json.c src/temperature.c
HEADERS := $(wildcard src/*.h)

.PHONY: all dev clean

all: ps5-signal.elf
dev: ps5-signal-dev.elf

ps5-signal.elf: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

ps5-signal-dev.elf: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -DSIGNAL_DEV -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f ps5-signal.elf ps5-signal-dev.elf
