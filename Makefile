prefix ?= /usr/local
bindir ?= $(prefx)/bin

PACKAGE ?= rept
VERSION ?= \(git\)

CFLAGS += -std=c99 -Wall -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\"

.PHONY: all check install uninstall clean

all: rept

check: rept
	./rept -v	> /dev/null

install: rept
	install rept $(bindir)/rept

uninstall:
	rm -f $(bindir)/rept

clean:
	rm -f rept
