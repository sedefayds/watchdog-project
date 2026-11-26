CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g -Iinclude
BUILD   := build

WATCHDOG_SRCS := src/main.c src/signal_manager.c src/process_manager.c src/logger.c
WATCHDOG_OBJS := $(WATCHDOG_SRCS:src/%.c=$(BUILD)/%.o)
HEADERS       := $(wildcard include/*.h)

.PHONY: all clean test

all: watchdog worker

watchdog: $(WATCHDOG_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

worker: workers/worker.c
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: all
	bash tests/test_scenarios.sh

clean:
	rm -rf $(BUILD) watchdog worker watchdog.log