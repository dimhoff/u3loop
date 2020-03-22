PREFIX=/usr/local
CFLAGS:=-std=c11 -Wall -Wextra -I/usr/include/libusb-1.0/
LDFLAGS:=-L/usr/lib/libusb-1.0/
LDLIBS:=-lusb-1.0 -lrt

.PHONY: all clean install

all: u3loop u3bench

clean:
	rm -f u3loop u3bench

install: u3loop u3bench
	install -D u3loop $(DESTDIR)$(PREFIX)/bin/u3loop
	install -D u3bench $(DESTDIR)$(PREFIX)/bin/u3bench
	install -D -m 644 u3loop.rules $(DESTDIR)/etc/udev/rules.d/99-u3loop.rules

u3loop: u3loop.c
u3bench: u3bench.c
