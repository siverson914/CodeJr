# KEYTEST

Records every scan code the keyboard interrupt sees, with its time, while
someone mashes the arrow keys, to find out why keys "get stuck" in games
on the real PCjr (see the comment at the top of `keytest.c` and
`lib/jrkeys.h`). Text only, ends by itself after 45 seconds, results in
`LOG.TXT`. Build: `apps/build.sh keytest`.
