# Makefile — the recipe that turns .c files into the omamd program.
#
# A Makefile is a list of targets and the commands that build them.
# `make` by itself builds the first target (`all`).
#
# pkg-config asks the system "what compiler flags does GTK need?"
# so we do not hard-code include paths that change between machines.

CC      ?= gcc
PKGS    := gtk+-3.0 webkit2gtk-4.1
CFLAGS  ?= -std=c11 -Wall -Wextra -g -O2
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDFLAGS ?=
LDLIBS  := $(shell pkg-config --libs $(PKGS))

SRC := src/main.c src/markdown.c
BIN := omamd

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC) src/markdown.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(BIN)

# Smoke-test the parser without opening a window.
test: $(BIN)
	./$(BIN) --html examples/welcome.md | grep -q '<h1>'
	./$(BIN) --html examples/welcome.md | grep -q '<code>'
	./$(BIN) --html examples/welcome.md | grep -q '<table>'
	./$(BIN) --html examples/welcome.md | grep -q '<input type="checkbox"'
	./$(BIN) --html examples/security.md | grep -q 'href="https://example.com/ok"'
	./$(BIN) --html examples/security.md | grep -q 'src="https://example.com/pix.png"'
	! ./$(BIN) --html examples/security.md | grep -q 'javascript:'
	! ./$(BIN) --html examples/security.md | grep -q 'file:///etc/passwd'
	! ./$(BIN) --html examples/security.md | grep -q 'data:text/html'
	! ./$(BIN) --html examples/security.md | grep -q 'src="/etc/passwd"'
	@echo "ok"
