#!/bin/sh
# Stands in for `claude -p`: writes a file in the working directory and
# prints a minimal stream-json transcript, like the real thing.
raw=$(cat)
# Keep the whole prompt for tests to check; one line of it goes in the reply.
printf '%s' "$raw" > .lastprompt
prompt=$(printf '%s' "$raw" | tr '\n' ' ' | sed 's/["\\]//g')
if [ "$prompt" = "stream" ]; then
	# Claude streams replies a few characters at a time.
	echo '{"type":"system","subtype":"init","session_id":"fake-session"}'
	i=0
	while [ $i -lt 60 ]; do
		echo '{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"word "}}}'
		i=$((i + 1))
	done
	echo '{"type":"result","subtype":"success","session_id":"fake-session","is_error":false,"result":"ok"}'
	exit 0
fi
printf '10 PRINT "HI"\n20 END\n' > HELLO.BAS
cat <<JSON
{"type":"system","subtype":"init","session_id":"fake-session"}
{"type":"stream_event","event":{"type":"message_start"}}
{"type":"assistant","message":{"content":[{"type":"tool_use","name":"Write","input":{"file_path":"$PWD/HELLO.BAS","content":"..."}}]}}
{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Made HELLO.BAS — you asked: $prompt"}}}
{"type":"result","subtype":"success","session_id":"fake-session","is_error":false,"result":"ok"}
JSON
