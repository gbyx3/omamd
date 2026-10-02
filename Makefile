# Makefile — the recipe that turns .c files into the omamd program.
#
# A Makefile is a list of targets and the commands that build them.
# `make` by itself builds the first target (`all`).
#
# pkg-config asks the system "what compiler flags does GTK need?"
# so we do not hard-code include paths that change between machines.

CC      ?= gcc
PKGS    := gtk+-3.0 webkit2gtk-4.1 fontconfig
CFLAGS  ?= -std=c11 -Wall -Wextra -g -O2
LDFLAGS ?=
HAVE_GTK := $(shell pkg-config --exists gtk+-3.0 webkit2gtk-4.1 fontconfig 2>/dev/null && echo yes)
ifeq ($(HAVE_GTK),yes)
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  := $(shell pkg-config --libs $(PKGS))
else
LDLIBS  :=
endif

SRC      := src/main.c src/markdown.c src/term.c src/theme.c src/html.c
BUILDDIR := build
BIN      := $(BUILDDIR)/omamd

CORE_SRC := src/core_smoke.c src/markdown.c src/theme.c src/html.c
COREBIN  := $(BUILDDIR)/omamd-html

PREFIX ?= $(HOME)/.local

.PHONY: all clean test test-core test-gtk install

all: $(BIN)

$(BIN): $(SRC) src/markdown.h src/term.h src/theme.h src/html.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

$(COREBIN): $(CORE_SRC) src/markdown.h src/theme.h src/html.h
	mkdir -p $(BUILDDIR)
	$(CC) -std=c11 -Wall -Wextra -g -O2 -o $@ $(CORE_SRC)

clean:
	rm -rf $(BUILDDIR)

# Parser + page wrapper, no GTK.  This is what `make test` runs on a Mac.
test-core: $(COREBIN)
	$(COREBIN) examples/welcome.md | grep -q '<h1>'
	$(COREBIN) examples/welcome.md | grep -q '<code>'
	$(COREBIN) examples/welcome.md | grep -q '<table>'
	$(COREBIN) examples/welcome.md | grep -q '<input type="checkbox"'
	$(COREBIN) examples/security.md | grep -q 'href="https://example.com/ok"'
	$(COREBIN) examples/security.md | grep -q 'src="https://example.com/pix.png"'
	! $(COREBIN) examples/security.md | grep -q 'javascript:'
	! $(COREBIN) examples/security.md | grep -q 'file:///etc/passwd'
	! $(COREBIN) examples/security.md | grep -q 'data:text/html'
	! $(COREBIN) examples/security.md | grep -q 'src="/etc/passwd"'
	$(COREBIN) --theme examples/colors.toml examples/welcome.md | grep -q '#1a1b26'
	$(COREBIN) --theme examples/colors.toml examples/welcome.md | grep -q '#c0caf5'
	$(COREBIN) --theme /no/such/omamd-theme.toml examples/welcome.md | grep -q '<h1>'
	$(COREBIN) examples/welcome.md | grep -q '@font-face'
	$(COREBIN) examples/welcome.md | grep -q 'file://'
	@echo "ok (core)"

# Full binary, including --term.  Needs gtk3 + webkit2gtk.
test-gtk: $(BIN)
	$(BIN) --html examples/welcome.md | grep -q '<h1>'
	$(BIN) --html examples/welcome.md | grep -q '<code>'
	$(BIN) --html examples/welcome.md | grep -q '<table>'
	$(BIN) --html examples/welcome.md | grep -q '<input type="checkbox"'
	$(BIN) --html examples/security.md | grep -q 'href="https://example.com/ok"'
	$(BIN) --html examples/security.md | grep -q 'src="https://example.com/pix.png"'
	! $(BIN) --html examples/security.md | grep -q 'javascript:'
	! $(BIN) --html examples/security.md | grep -q 'file:///etc/passwd'
	! $(BIN) --html examples/security.md | grep -q 'data:text/html'
	! $(BIN) --html examples/security.md | grep -q 'src="/etc/passwd"'
	$(BIN) --term examples/welcome.md | grep -q 'Welcome'
	! $(BIN) --term examples/welcome.md | grep -q '<h1>'
	$(BIN) --theme examples/colors.toml --html examples/welcome.md | grep -q '#1a1b26'
	$(BIN) --theme examples/colors.toml --html examples/welcome.md | grep -q '#c0caf5'
	$(BIN) --theme /no/such/omamd-theme.toml --html examples/welcome.md | grep -q '<h1>'
	@echo "ok (gtk)"

test: test-core
ifeq ($(HAVE_GTK),yes)
test: test-gtk
endif

install: $(BIN) pkgbuild/omamd.desktop pkgbuild/omamd.svg
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/omamd
	install -Dm644 pkgbuild/omamd.desktop $(DESTDIR)$(PREFIX)/share/applications/omamd.desktop
	install -Dm644 pkgbuild/omamd.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/omamd.svg
	install -Dm644 fonts/OFL.txt $(DESTDIR)$(PREFIX)/share/omamd/fonts/OFL.txt
	install -Dm644 fonts/iAWriterMonoS-Regular.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-Regular.ttf
	install -Dm644 fonts/iAWriterMonoS-Italic.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-Italic.ttf
	install -Dm644 fonts/iAWriterMonoS-Bold.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-Bold.ttf
	install -Dm644 fonts/iAWriterMonoS-BoldItalic.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-BoldItalic.ttf
	install -Dm644 examples/colors.toml $(DESTDIR)$(PREFIX)/share/omamd/colors.toml
	-update-desktop-database $(DESTDIR)$(PREFIX)/share/applications
	-gtk-update-icon-cache -q -t -f $(DESTDIR)$(PREFIX)/share/icons/hicolor
