# bento_ttymidi, built on the Raspberry Pi (build-essential, libasound2-dev).
#   make         build/bento_ttymidi
#   make test    unit tests; test_midi_convert only where libasound is installed
#   make clean

CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -Wall -Wextra
LDLIBS   += -lasound -lpthread

COMMIT   := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
DIRTY    := $(shell git status --porcelain --untracked-files=no 2>/dev/null | grep -q . && echo -dirty)
CPPFLAGS += -DBENTO_TTYMIDI_COMMIT='"$(COMMIT)$(DIRTY)"'

BUILD    := build
TARGET   := $(BUILD)/bento_ttymidi
SRCS     := src/main.c src/uart.c src/alsa_seq.c src/midi_convert.c src/midi_parser.c
OBJS     := $(SRCS:src/%.c=$(BUILD)/%.o)
HEADERS  := $(wildcard src/*.h)

HAVE_ALSA := $(shell pkg-config --exists alsa 2>/dev/null && echo 1)

.PHONY: all test clean FORCE

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# main.o carries the commit, rebuild it when the commit changes
$(BUILD)/main.o: $(BUILD)/commit.stamp
$(BUILD)/commit.stamp: FORCE | $(BUILD)
	@echo '$(COMMIT)$(DIRTY)' | cmp -s - $@ || echo '$(COMMIT)$(DIRTY)' > $@

$(BUILD):
	mkdir -p $@

test: $(BUILD)/test_midi_parser $(if $(HAVE_ALSA),$(BUILD)/test_midi_convert)
	$(BUILD)/test_midi_parser
ifeq ($(HAVE_ALSA),1)
	$(BUILD)/test_midi_convert
else
	@echo "test_midi_convert skipped: no libasound (pkg-config alsa)"
endif

$(BUILD)/test_midi_parser: tests/unit/test_midi_parser.c tests/unit/check.h src/midi_parser.c src/midi_parser.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/unit/test_midi_parser.c src/midi_parser.c

$(BUILD)/test_midi_convert: tests/unit/test_midi_convert.c tests/unit/check.h src/midi_convert.c src/midi_convert.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/unit/test_midi_convert.c src/midi_convert.c -lasound

clean:
	rm -rf $(BUILD)
