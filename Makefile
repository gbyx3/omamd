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
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDFLAGS ?=
LDLIBS  := $(shell pkg-config --libs $(PKGS))

SRC      := src/main.c src/markdown.c src/term.c
BUILDDIR := build
BIN      := $(BUILDDIR)/omamd

PREFIX ?= $(HOME)/.local

.PHONY: all clean test install

all: $(BIN)

$(BIN): $(SRC) src/markdown.h src/term.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILDDIR)

# Smoke-test the parser without opening a window.
test: $(BIN)
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
	@echo "ok"

install: $(BIN) pkgbuild/omamd.desktop pkgbuild/omamd.svg
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/omamd
	install -Dm644 pkgbuild/omamd.desktop $(DESTDIR)$(PREFIX)/share/applications/omamd.desktop
	install -Dm644 pkgbuild/omamd.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/omamd.svg
	install -Dm644 fonts/OFL.txt $(DESTDIR)$(PREFIX)/share/omamd/fonts/OFL.txt
	install -Dm644 fonts/iAWriterMonoS-Regular.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-Regular.ttf
	install -Dm644 fonts/iAWriterMonoS-Italic.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-Italic.ttf
	install -Dm644 fonts/iAWriterMonoS-Bold.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-Bold.ttf
	install -Dm644 fonts/iAWriterMonoS-BoldItalic.ttf $(DESTDIR)$(PREFIX)/share/omamd/fonts/iAWriterMonoS-BoldItalic.ttf
	-update-desktop-database $(DESTDIR)$(PREFIX)/share/applications
	-gtk-update-icon-cache -q -t -f $(DESTDIR)$(PREFIX)/share/icons/hicolor
