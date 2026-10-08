#!/bin/sh
# Overlay chrome contracts for GitHub #9 and #10, plus Linux GTK cluster.
# Not part of `make test` (Linux stays gcc-only). Run: make test-overlay
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
fail=0

ok() { printf 'ok %s\n' "$1"; }
bad() { printf 'FAIL %s\n' "$1"; fail=1; }

VIEWER=apple/Omamd/Viewer.swift
CONTENT=apple/Omamd/ContentView.swift
APP=apple/Omamd/OmamdApp.swift
CHROME=apple/Omamd/MarkdownWebView.swift
GTK=linux/gtk.c

# --- #9 Mac theme drop-up menu ---
if grep -q 'showSettingsWindow' "$VIEWER"; then
	bad '#9 Mac theme still sends showSettingsWindow (Settings scene / yabai)'
else
	ok '#9 no showSettingsWindow'
fi
if grep -E -q 'Menu\(|NSMenu|NSPopUpButton' "$CONTENT" "$VIEWER"; then
	ok '#9 native Menu/NSMenu present'
else
	bad '#9 no native Menu/NSMenu for the palette icon'
fi

# --- #10 Follow control ---
if grep -q 'arrow.down.to.line' "$CONTENT"; then
	bad '#10 Follow still uses arrow.down.to.line (reads as download)'
else
	ok '#10 Follow icon is not download'
fi
if grep -q 'followEnabled' "$VIEWER" && grep -q 'toggleFollow' "$VIEWER"; then
	ok '#10 Follow toggle still exists'
else
	bad '#10 Follow feature was removed'
fi

# --- #7 yabai native window tabs ---
if grep -q 'allowsAutomaticWindowTabbing = false' "$APP" \
	&& grep -q 'tabbingMode = .disallowed' "$CHROME"; then
	ok '#7 native window tabbing disabled'
else
	bad '#7 window tabbing still allowed (yabai retile on tab switch)'
fi

# --- Linux overlay cluster (Open / Theme / Follow) ---
if grep -q 'omamd-overlay-cluster' "$GTK" \
	&& grep -q 'open_btn' "$GTK" \
	&& grep -q 'theme_btn' "$GTK" \
	&& grep -q 'follow_btn' "$GTK"; then
	ok 'linux overlay cluster Open/Theme/Follow'
else
	bad 'linux overlay missing bottom-right Open/Theme/Follow cluster'
fi
if grep -q 'follow_enabled' "$GTK" && grep -q 'on_follow_clicked' "$GTK"; then
	ok 'linux Follow is a pin toggle'
else
	bad 'linux Follow toggle missing'
fi
if grep -q 'on_theme_clicked' "$GTK" && grep -q 'Choose File' "$GTK"; then
	ok 'linux Theme menu'
else
	bad 'linux Theme overlay menu missing'
fi
if grep -q 'arrow.down.to.line' "$GTK"; then
	bad 'linux Follow still uses a download glyph'
else
	ok 'linux Follow icon is not download'
fi

if [ "$fail" -ne 0 ]; then
	echo 'overlay chrome: FAIL'
	exit 1
fi
echo 'ok (overlay chrome)'
