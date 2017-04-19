prefix ?= /usr/local
bindir ?= $(prefix)/bin

PACKAGE ?= rept
VERSION ?= \(git\)

CFLAGS += -std=c99 -Wall -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\"

.PHONY: all check install uninstall clean

all: rept

check: rept
	./rept -v > /dev/null

install: rept
	install rept $(bindir)/rept

uninstall:
	$(RM) $(bindir)/rept

clean:
	$(RM) rept
