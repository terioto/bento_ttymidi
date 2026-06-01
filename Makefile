CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lasound -lpthread
TARGET  = bento_ttymidi
SRC     = bento_ttymidi.c
PREFIX  = /usr/local

.PHONY: all install clean dist

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

dist:
	./setup/pack_bento_ttymidi.sh
