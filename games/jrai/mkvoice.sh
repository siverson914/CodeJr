#!/bin/sh
# Makes JRAI's voice clips: voice/lines.txt -> voice/NAME.mp3 (ElevenLabs
# through gipity, made once and kept) -> voice/out/NAME.SND (speech/mksnd.py
# at its default quality, the clips SAY.EXE plays best).
#   games/jrai/mkvoice.sh          make what's missing
#   games/jrai/mkvoice.sh JM4      remake one line's audio (after editing its text)
set -e
cd "$(dirname "$0")/voice"
MKSND=../../../speech/mksnd.py
mkdir -p out
voice_id() {
	case $1 in
	ai) echo zfWx5IjUaWGWTId9hv9t ;;  # Atlas: wise, deep
	jr) echo a0euEDMIIr9cUObJf0DX ;;  # Brittney: a child
	ann) echo YOq2y2Up4RgXP2HyXjE5 ;; # the deep game announcer
	*) echo "$1" ;;
	esac
}
grep -v '^#' lines.txt | while IFS='|' read -r name who text; do
	[ -n "$name" ] || continue
	if [ "$1" = "$name" ]; then rm -f "$name.mp3"; fi
	if [ ! -f "$name.mp3" ]; then
		echo "$name: speech from gipity ($who)"
		$MKSND --text "$text" --voice "$(voice_id "$who")" --keep -o "out/$name"
		mv "out/$name.mp3" "$name.mp3"
	elif [ ! -f "out/$name.SND" ] || [ "$name.mp3" -nt "out/$name.SND" ] || [ lines.txt -nt "out/$name.SND" ]; then
		$MKSND "$name.mp3" --title "$text" -o "out/$name"
	fi
done
ls -l out/*.SND | awk '{s += $5; print $5, $NF} END {print s, "bytes in all"}'
