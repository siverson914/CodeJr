#!/bin/sh
# Copies the sources of the programs CodeJr wrote on the PCjr from the live
# workspace into apps/, so they are in git. The workspace stays the working
# copy (CodeJr edits it there); run this after a session that changed one.
#   apps/save-projects.sh [workspace]
# Folders this repo deploys itself (and CodeJr's own) are skipped.
set -e
cd "$(dirname "$0")"
ws=${1:-${CODEJR_HOME:-$HOME/.local/share/codejr}/workspace}
skip=" JRAI JRCUBE JRDOOM JRFLIGHT SAY SOPWITH JRGFX VOICE UTILS "
n=0
for d in "$ws"/*/; do
	name=$(basename "$d")
	case "$skip" in *" $name "*) continue ;; esac
	dest=$(echo "$name" | tr A-Z a-z)
	found=0
	for f in "$d"*; do
		[ -f "$f" ] || continue
		base=$(basename "$f")
		case $(echo "$base" | tr a-z A-Z) in
		LOG.TXT | SHOT.* | SCREEN.TXT | SAYDUMP.BIN) continue ;;
		*.C | *.H | *.ASM | *.INC | *.BAS | *.TXT | *.BAT | *.PAS) ;;
		*) continue ;;
		esac
		mkdir -p "$dest"
		cp "$f" "$dest/$(echo "$base" | tr A-Z a-z)"
		found=1
	done
	if [ $found = 1 ]; then
		n=$((n + 1))
		echo "saved $name -> apps/$dest"
		[ -f "$dest/README.md" ] || printf '# %s\n\nWritten with CodeJr on the PCjr. The working copy is the `%s` folder of\nthe live workspace (`C:\\CODEJR\\PROGS\\%s` on the PCjr); `just save-projects`\ncopies its sources here. Build: `apps/build.sh %s`.\n' "$name" "$name" "$name" "$dest" > "$dest/README.md"
	fi
done
# What CodeJr added to the library from the PCjr side (the workspace's
# JRGFX folder, minus the headers the server keeps there) goes to
# lib/shared, which every build includes.
for f in "$ws"/JRGFX/*; do
	[ -f "$f" ] || continue
	base=$(basename "$f" | tr A-Z a-z)
	case "$base" in *.c | *.h | *.asm | *.inc) ;; *) continue ;; esac
	[ -f "../lib/$base" ] && continue
	mkdir -p ../lib/shared
	cp "$f" "../lib/shared/$base"
	echo "saved JRGFX/$base -> lib/shared"
done
echo "$n project(s) saved"
