prefix ?= /usr/local
bindir ?= $(prefix)/bin
mandir ?= $(prefix)/share/man/man1

PACKAGE ?= rept
VERSION ?= \(git\)

MAN ?= man

CFLAGS += -std=c99 -Wall -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\"

.PHONY: all check install uninstall clean distclean

all: rept Readme.txt

Readme.txt: rept.1
	$(MAN) ./rept.1 > Readme.txt

check: rept
	./rept -v > /dev/null

install: rept
	install rept $(bindir)/
	install rept.1 $(mandir)/

uninstall:
	$(RM) $(bindir)/rept $(mandir)/rept.1

clean:
	$(RM) rept

distclean: clean
	$(RM) Readme.txt
