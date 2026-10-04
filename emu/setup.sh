#!/bin/sh
# Builds emu/c, the emulated PCjr's drive C: MS-Kermit 3.14 plus the CodeJr
# launcher on COM1 (DOSBox's serial port goes straight to the server).
set -e
cd "$(dirname "$0")"
if [ ! -f msk314/KERMIT.EXE ]; then
	# columbia.edu blocks scripted downloads; the Wayback Machine has the same zip.
	curl -sL -o msk314.zip "https://web.archive.org/web/2020id_/https://www.columbia.edu/kermit/ftp/archives/msk314.zip"
	unzip -oq msk314.zip -d msk314 && rm msk314.zip
fi
mkdir -p c/MSK314 c/CODEJR
cp msk314/KERMIT.EXE c/MSK314/
(cd ../server && go build -o codejr ./cmd/codejr)
../server/codejr -write-dos c -com COM1 -dos-host 127.0.0.1
echo "emu/c ready"
