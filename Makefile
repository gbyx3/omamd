# Makefile — the recipe that turns .c files into the omamd program.
#
# A Makefile is a list of targets and the commands that build them.
# `make` by itself builds the first target (`all`).
#
# pkg-config asks the system "what compiler flags does GTK need?"
# so we do not hard-code include paths that change between machines.
# The CLI (--html / --term) compiles without those libraries.

CC      ?= gcc
PKGS    := gtk+-3.0 webkit2gtk-4.1 fontconfig
CFLAGS  ?= -std=c11 -Wall -Wextra -g -O2
CLI_CFLAGS ?= -std=c11 -Wall -Wextra -g -O2
LDFLAGS ?=
HAVE_GTK := $(shell pkg-config --exists gtk+-3.0 webkit2gtk-4.1 fontconfig 2>/dev/null && echo yes)
ifeq ($(HAVE_GTK),yes)
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  := $(shell pkg-config --libs $(PKGS))
else
LDLIBS  :=
endif

BUILDDIR := build
INCLUDES := -I core -I cli
CORE     := core/util.c core/fonts.c core/markdown.c core/theme.c core/html.c core/term.c
CLI_SRC  := cli/cli.c
COMMON   := $(CLI_SRC) $(CORE)
HDRS     := core/omamd.h core/util.h core/fonts.h core/markdown.h core/theme.h core/html.h core/term.h cli/cli.h

ifeq ($(HAVE_GTK),yes)
BIN      := $(BUILDDIR)/omamd
CLI_BIN  := $(BUILDDIR)/omamd-cli
else
BIN      := $(BUILDDIR)/omamd
CLI_BIN  := $(BUILDDIR)/omamd
endif

PREFIX ?= $(HOME)/.local

.PHONY: all clean test test-cli test-gtk install

all: $(BIN)
ifeq ($(HAVE_GTK),yes)
all: $(CLI_BIN)
endif

$(CLI_BIN): cli/main.c $(COMMON) $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(CLI_CFLAGS) $(INCLUDES) -o $@ cli/main.c $(COMMON)

ifeq ($(HAVE_GTK),yes)
$(BIN): linux/gtk.c $(COMMON) $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ linux/gtk.c $(COMMON) $(LDFLAGS) $(LDLIBS)
endif

clean:
	rm -rf $(BUILDDIR)

# --html and --term, no GTK.  This is `make test` on a Mac.
test-cli: $(CLI_BIN)
	$(CLI_BIN) --html examples/welcome.md | grep -q '<h1>'
	$(CLI_BIN) --html examples/welcome.md | grep -q '<code>'
	$(CLI_BIN) --html examples/welcome.md | grep -q '<table>'
	$(CLI_BIN) --html examples/welcome.md | grep -q '<input type="checkbox"'
	$(CLI_BIN) --html examples/security.md | grep -q 'href="https://example.com/ok"'
	$(CLI_BIN) --html examples/security.md | grep -q 'src="https://example.com/pix.png"'
	! $(CLI_BIN) --html examples/security.md | grep -q 'javascript:'
	! $(CLI_BIN) --html examples/security.md | grep -q 'file:///etc/passwd'
	! $(CLI_BIN) --html examples/security.md | grep -q 'data:text/html'
	! $(CLI_BIN) --html examples/security.md | grep -q 'src="/etc/passwd"'
	$(CLI_BIN) --term examples/welcome.md | grep -q 'Welcome'
	! $(CLI_BIN) --term examples/welcome.md | grep -q '<h1>'
	$(CLI_BIN) --theme examples/colors.toml --html examples/welcome.md | grep -q '#fff8ee'
	$(CLI_BIN) --theme examples/colors.toml --html examples/welcome.md | grep -q '#c81e1e'
	! $(CLI_BIN) --html examples/welcome.md | grep -q '#fff8ee'
	$(CLI_BIN) --html examples/welcome.md | grep -q '#1e1e2e'
	$(CLI_BIN) --theme /no/such/omamd-theme.toml --html examples/welcome.md | grep -q '<h1>'
	$(CLI_BIN) --html examples/welcome.md | grep -q '@font-face'
	$(CLI_BIN) --html examples/welcome.md | grep -q 'file://'
	$(CLI_BIN) --version | grep -q 'omamd'
	@echo "ok (cli)"

# GTK binary still dispatches --html / --term through cli.c.
test-gtk: $(BIN)
	$(BIN) --html examples/welcome.md | grep -q '<h1>'
	$(BIN) --term examples/welcome.md | grep -q 'Welcome'
	$(BIN) --theme examples/colors.toml --html examples/welcome.md | grep -q '#fff8ee'
	@echo "ok (gtk)"

test: test-cli
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
