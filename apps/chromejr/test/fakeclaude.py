#!/usr/bin/env python3
# A stand-in for `claude -p` in ChromeJr's tests: reads the page, answers
# with a fixed layout for it in Claude Code's stream-json, a few
# characters at a time as the real one does.
import json, sys

page = sys.stdin.read()
if "TITLE: Blue Widget" in page:
    reply = ("KIND product\nTITLE Blue Widget\nP A blue widget for six dollars. It is calm and blue.\n"
             "A 2 | Add to cart\nA 1 | Back to the shop\n")
else:
    reply = ("KIND list\nTITLE Test Shop\nF 1 | Search\nI 2 | Red Widget | Shiny and red | $5.00\n"
             "I 3 | Blue Widget | Calm and blue | $6.00\nA 4 | Add to cart\n")
for i in range(0, len(reply), 7):
    print(json.dumps({"type": "stream_event", "event": {"type": "content_block_delta",
          "delta": {"type": "text_delta", "text": reply[i:i + 7]}}}), flush=True)
print(json.dumps({"type": "result", "result": reply, "is_error": False, "total_cost_usd": 0,
                  "usage": {"input_tokens": len(page) // 4, "output_tokens": len(reply) // 4}}))
