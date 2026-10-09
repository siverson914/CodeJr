#!/bin/sh
# Stands in for `claude -p`: writes a file in the working directory and
# prints a minimal stream-json transcript, like the real thing.
raw=$(cat)
# Keep the whole prompt for tests to check; one line of it goes in the reply.
printf '%s' "$raw" > .lastprompt
prompt=$(printf '%s' "$raw" | tr '\n' ' ' | sed 's/["\\]//g')
if [ "$prompt" = "long" ]; then
	# A reply longer than the screen, one line per delta.
	echo '{"type":"system","subtype":"init","session_id":"fake-session"}'
	i=1
	while [ $i -le 60 ]; do
		echo '{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"line '$i'\n"}}}'
		i=$((i + 1))
	done
	echo '{"type":"result","subtype":"success","session_id":"fake-session","is_error":false,"result":"ok"}'
	exit 0
fi
if [ "$prompt" = "big" ]; then
	# A turn at the end of a long conversation: thought, a file written
	# slowly, and a size past the tidy-up line.
	cat <<JSON
{"type":"system","subtype":"init","session_id":"fake-session"}
{"type":"system","subtype":"thinking_tokens","estimated_tokens":1200}
{"type":"stream_event","event":{"type":"content_block_start","content_block":{"type":"tool_use","name":"Write"}}}
{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"input_json_delta","partial_json":"{\\"file_path\\": \\"/w/BIG.C\\", \\"content\\": \\"int"}}}
{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"A big answer."}}}
{"type":"result","subtype":"success","session_id":"fake-session","is_error":false,"result":"ok","usage":{"input_tokens":2,"cache_read_input_tokens":100000,"cache_creation_input_tokens":19000,"output_tokens":998}}
JSON
	exit 0
fi
if [ "$prompt" = "/compact" ]; then
	cat <<JSON
{"type":"system","subtype":"status","status":"compacting","session_id":"fake-session"}
{"type":"system","subtype":"compact_boundary","session_id":"fake-session","compact_metadata":{"trigger":"manual","pre_tokens":120000,"post_tokens":900}}
{"type":"result","subtype":"success","session_id":"fake-session","is_error":false,"result":""}
JSON
	exit 0
fi
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
