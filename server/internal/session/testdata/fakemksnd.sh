#!/bin/sh
# Stands in for speech/mksnd.py in tests (no voice service): any
# --text ... -o OUT makes OUT.SND from a stored clip.
while [ $# -gt 0 ]; do
	case $1 in -o) out=$2; shift ;; esac
	shift
done
cp "$(dirname "$0")/HI.SND" "$out.SND"
