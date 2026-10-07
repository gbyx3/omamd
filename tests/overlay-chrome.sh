#!/bin/sh
# Overlay chrome contracts for GitHub #9 and #10.
# Not part of `make test` (Linux stays gcc-only). Run: make test-overlay
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
fail=0

ok() { printf 'ok %s\n' "$1"; }
bad() { printf 'FAIL %s\n' "$1"; fail=1; }

VIEWER=apple/Omamd/Viewer.swift
CONTENT=apple/Omamd/ContentView.swift

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

if [ "$fail" -ne 0 ]; then
	echo 'overlay chrome: FAIL (open issues #9 #10)'
	exit 1
fi
echo 'ok (overlay chrome)'
