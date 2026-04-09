CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?=
LDFLAGS ?=
PREFIX ?= /opt/Dell1320
PPD_DIR ?= /usr/share/ppd/Dell
DESTDIR ?=
PKG_CONFIG ?= pkg-config
GS ?= gs
VERSION ?= 0.1.0
ARCH ?= $(shell uname -m)

CUPS_CFLAGS := $(shell $(PKG_CONFIG) --cflags cups 2>/dev/null)
CUPS_LIBS := $(shell $(PKG_CONFIG) --libs cups 2>/dev/null || printf '%s' '-lcups')

BINDIR := bin
SRCDIR := src
SCRIPTDIR := scripts
DISTDIR := dist
DISTNAME := dell-1320c-cups-driver-linux-$(ARCH)-v$(VERSION)

FILTERS := FXM_PF FXM_MF FXM_PM2FXR FXM_SBP FXM_PR FXM_CC FXM_ALC FXM_HBPL

all: check-deps $(addprefix $(BINDIR)/,$(FILTERS))

check-deps:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Missing compiler: $(CC)" >&2; exit 1; }
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || { echo "Missing pkg-config" >&2; exit 1; }
	@$(PKG_CONFIG) --exists cups || { echo "Missing CUPS development files (pkg-config cups)" >&2; exit 1; }
	@command -v $(GS) >/dev/null 2>&1 || { echo "Missing Ghostscript runtime ($(GS)) required by scripts/FXM_PS2PM" >&2; exit 1; }

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/FXM_PF: $(SRCDIR)/FXM_PF.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $(SRCDIR)/FXM_PF.c $(LDFLAGS) $(CUPS_LIBS)

$(BINDIR)/FXM_MF: $(SRCDIR)/FXM_MF.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCDIR)/FXM_MF.c $(LDFLAGS)

$(BINDIR)/FXM_PM2FXR: $(SRCDIR)/FXM_PM2FXR.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $(SRCDIR)/FXM_PM2FXR.c $(LDFLAGS) $(CUPS_LIBS) -lm

$(BINDIR)/FXM_SBP: $(SRCDIR)/FXM_SBP.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $(SRCDIR)/FXM_SBP.c $(LDFLAGS) $(CUPS_LIBS)

$(BINDIR)/FXM_PR: $(SRCDIR)/FXM_PR.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $(SRCDIR)/FXM_PR.c $(LDFLAGS) $(CUPS_LIBS)

$(BINDIR)/FXM_CC: $(SRCDIR)/FXM_CC.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $(SRCDIR)/FXM_CC.c $(LDFLAGS) $(CUPS_LIBS)

$(BINDIR)/FXM_ALC: $(SRCDIR)/FXM_ALC.c $(SRCDIR)/sq21_simple.c $(SRCDIR)/sq21_simple.h | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCDIR)/FXM_ALC.c $(SRCDIR)/sq21_simple.c $(LDFLAGS)

$(BINDIR)/FXM_HBPL: $(SRCDIR)/FXM_HBPL.c | $(BINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $(SRCDIR)/FXM_HBPL.c $(LDFLAGS) $(CUPS_LIBS)

install: all
	install -d $(DESTDIR)$(PREFIX)/filter
	install -d $(DESTDIR)$(PPD_DIR)
	install -m 755 $(BINDIR)/FXM_PF $(DESTDIR)$(PREFIX)/filter/FXM_PF
	install -m 755 $(BINDIR)/FXM_MF $(DESTDIR)$(PREFIX)/filter/FXM_MF
	install -m 755 $(BINDIR)/FXM_PM2FXR $(DESTDIR)$(PREFIX)/filter/FXM_PM2FXR
	install -m 755 $(BINDIR)/FXM_SBP $(DESTDIR)$(PREFIX)/filter/FXM_SBP
	install -m 755 $(BINDIR)/FXM_PR $(DESTDIR)$(PREFIX)/filter/FXM_PR
	install -m 755 $(BINDIR)/FXM_CC $(DESTDIR)$(PREFIX)/filter/FXM_CC
	install -m 755 $(BINDIR)/FXM_ALC $(DESTDIR)$(PREFIX)/filter/FXM_ALC
	install -m 755 $(BINDIR)/FXM_HBPL $(DESTDIR)$(PREFIX)/filter/FXM_HBPL
	install -m 755 $(SCRIPTDIR)/FXM_PS2PM $(DESTDIR)$(PREFIX)/filter/FXM_PS2PM
	install -m 644 ppd/Dell-1320c.ppd $(DESTDIR)$(PPD_DIR)/Dell-1320c.ppd

dist: all
	rm -rf $(DISTDIR)/$(DISTNAME)
	mkdir -p $(DISTDIR)/$(DISTNAME)/bin $(DISTDIR)/$(DISTNAME)/ppd $(DISTDIR)/$(DISTNAME)/scripts
	cp -a $(BINDIR)/. $(DISTDIR)/$(DISTNAME)/bin/
	cp -a $(SCRIPTDIR)/FXM_PS2PM $(DISTDIR)/$(DISTNAME)/scripts/
	cp -a ppd/Dell-1320c.ppd $(DISTDIR)/$(DISTNAME)/ppd/
	cp -a README.md INSTALL.md install.sh $(DISTDIR)/$(DISTNAME)/
	tar -C $(DISTDIR) -czf $(DISTDIR)/$(DISTNAME).tar.gz $(DISTNAME)
	sha256sum $(DISTDIR)/$(DISTNAME).tar.gz > $(DISTDIR)/$(DISTNAME).tar.gz.sha256

clean:
	rm -rf $(BINDIR) $(DISTDIR)

.PHONY: all check-deps install dist clean
