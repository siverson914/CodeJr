# ChromeJr: handoff for the next session

Read this, then `apps/chromejr/README.md` (what the prototype is and what
it measured), then the repo's `docs/HANDOFF.md` "Hard-won facts" for the
serial link. Work in a gw session as usual; land with `gw done`.

## Where it stands (2026-10-10)

A working prototype, landed and running:

- **Laptop half:** `server/cmd/chromejr` and `server/internal/web`.
  Headless Chromium (driven with `chromedp`, pinned at v0.11.2; the
  newest version has a different, generic API). `extract.js` runs in the
  page and turns it into lines of styled text with every link, button
  and field numbered (`data-cj="n"` on the element so it can be found
  again). `session.go` takes typed commands; `serve.go` talks to the PCjr.
- **PCjr half:** for now the CodeJr client (`CODEJR.EXE`) dialing port
  4850, launched by `C:\CODEJR\PROGS\CHROMEJR\CHROMEJR.BAT` (`/run` in
  CodeJr, then CHROMEJR). It gives scrollback, paging, a status bar and
  an input line. The header still says "CodeJr".
- **Running it:** `just chromejr` (server, log in
  `~/.local/share/chromejr/server.log`), `just chromejr-stop`,
  `just chromejr-text URL` (browse in this terminal, as the PCjr would
  see it: the fastest way to iterate on page conversion),
  `just chromejr-login` (a real window to sign in to a site once by hand).
  Port 4850 is open in the laptop's firewall for 192.168.0.0/24 (ufw);
  a new port needs a new rule, which needs Steve's sudo.

## Done 2026-10-10: the LLM page filter

Built as designed below, with three changes found by measuring (the
README's "The page filter" has the details and the timings):

- **Lines, not JSON, from the model** (`I 12 | title | detail | price`):
  half the output tokens, and output tokens are the time.
- **`--effort low`**: Haiku through `claude -p` thought for 900 to 1,800
  tokens a page by default (8 to 11 s). Low effort brings most pages to
  3 to 4 s. No flag turns thinking off entirely; the 6 to 9 s pages are
  the ones it still thinks on.
- **The view's own numbers (1, 2, 3)**, mapped to the page's `data-cj`
  numbers on the laptop. `w` is now the raw page (`w` again: the whole
  page), `v` the simple view, `f` the filter off and on.

Code: `server/internal/web/filter.go` (running the model, checking each
line against the page, the cache, sections), `prompt.go`, `markup.go`
(Junior markup and how the CodeJr client draws it), `filter_test.go`.
Saved pages for tuning: `server/internal/web/testdata/` (11 pages),
`just chromejr-try`.

**Not tried yet:** any of it on the real PCjr or in the emulator. The
server's `Serve` passes the filter in, so `just chromejr` (restart it)
then CHROMEJR on the PCjr is the next test. Expect 3 to 5 s on the
laptop per new page before the first byte goes down the line, where
there used to be about 2.

## Next, in order

1. **Try it on the real PCjr** through the CodeJr client. Watch whether
   the 3 to 5 s wait feels long; the client shows the status line
   ("Simplifying the page...") but no progress. If it does feel long:
   stream the model's reply (`--output-format stream-json`) and send
   each block as soon as its line is complete.
2. **Tune the views on the showcase sites** with the saved pages. Known
   rough spots: Wikipedia offers subsections as sections (Hardware stops
   at Cartridges); the model paraphrases leads a little; Amazon's color
   buttons are labeled 0, 1, 2 on the page and the model names them
   from the text around them; Google search gets a robot check.
3. **The PCjr program** (item 6 of the design below), reading Junior
   markup directly: arrow keys between the numbered blocks, Enter to
   pick, its own colors.

## What Steve thought after trying it on the real PCjr

> "It's tolerable. It's kind of cool that it seems to sort of work. But
> it's just very ugly. ... it might be a huge task to make it work really
> good."

His log (2026-10-10, 10:11 am): amazon.com in 1.9 s (54 lines), then a
"Play" button, a category link, a product. Pages arrive fast enough. The
problem is what arrives: a wall of numbered fragments, 120 numbers on
Amazon's front page.

He is clear about the goal: **a tech demo, something cool to show off,
not a browser people will use.** So the bar is "looks good on the
showcase sites", not "works on every site".

## The idea for this session: an LLM between the page and the PCjr

Steve's words, condensed: get the page's content, then have an LLM on
the laptop decide what matters and lay it out for the Junior.

- **Actions:** "what are the five most important actions somebody could
  do here?"
- **Lists:** if the page lists items (search results, products), "what
  are the ten items to list", numbered and pretty.
- **Articles:** Wikipedia reformatted so it is presentable: title, a
  short lead, sections the user can open by number.
- **Google's home page** becomes a search box and maybe one other
  choice. A search becomes a search box plus numbered results; press 3,
  go there, and that page is simplified in turn.
- Output in **"a Junior version of markup"**: a small format the PCjr
  half draws with colors and layout, not raw text.

## A design to start from (change it if something better shows up)

1. **Keep the browser and the numbering.** The real page and its
   `data-cj` numbers stay the source of truth: every action the LLM
   offers must point at a real numbered element, so pressing it is the
   existing `Browser.Press`/`Fill`. The LLM never invents a URL or a
   click it can't back with an element.
2. **Feed the LLM the extract, not the HTML.** `extract.js` already
   gives compact text with numbers (Amazon search: 15 KB; Wikipedia main
   part: 54 KB). Send that plus the URL and title, ask for JSON:
   `{kind: "search|list|article|form|other", title, blocks: [...],
   actions: [{label, target: n, input?: true}]}`. Validate every target
   number against the page's targets; drop what doesn't match.
3. **Model and cost.** Use the Claude API directly (the laptop has a
   Claude subscription through Claude Code; an API key may not exist:
   ask Steve) or run `claude -p` the way CodeJr's server does
   (`server/internal/claude`). A small, fast model is the right default
   for a page filter (Haiku 4.5: `claude-haiku-4-5-20251001`); latency
   matters more than depth. Cache by URL plus a hash of the extract, so
   Back and reloads are instant. Measure: time per page, tokens per page.
4. **"Junior markup".** A handful of block types the PCjr can draw
   well in 80x25 and 16 colors: heading, paragraph, numbered item
   (title, one detail line, price), action bar, input box, divider,
   "more" marker. Send it as a compact text format, not JSON, to the
   PCjr. Fall back to today's raw extract whenever the LLM fails, times
   out or is unsure (`w` already shows the raw page; keep it as
   "raw view").
5. **Showcase sites first:** Google (or DuckDuckGo) home and results,
   Wikipedia, Amazon home, search, product page, cart, Hacker News, a
   news site. Get each one looking good before generalizing. Keep a
   folder of saved extracts (`server/internal/web/testdata/`) so the
   prompt can be tuned and tested without loading live pages.
6. **Then the PCjr program** (`CHROMEJR.EXE`, `apps/chromejr`), only if
   the filtered pages look good through the CodeJr client: its own
   screen and colors, arrow keys to move between actions with Enter to
   pick (no typing numbers), an address bar, pictures (the laptop cuts a
   picture out with `Browser.Shot`, dithers to 160x200 in 16 colors; the
   PCjr shows it in graphics mode). Build it on `lib/` (JRGFX, jrkeys)
   and the link code in `client/`.

## Rules that apply here

- The project's `CLAUDE.md` rules all apply (no em dashes, 8088 only for
  PCjr code, never open a window on Steve's screen: Chromium runs
  headless; `just chromejr-login` is the only exception, on purpose).
- Never send, post or publish anything without asking. Never place an
  order or press "Buy" in a test. Test with an empty cart and no
  sign-in unless Steve says otherwise.
- Passwords and card numbers must not be typed on the PCjr: what it
  types crosses the house network unencrypted. Sign in on the laptop.
- Say which results are from the terminal (`-text`), the emulator, or
  the real PCjr.

## Known rough edges in the prototype

- Numbers shift between loads of the same page; `.words` (press by
  label) exists because of it.
- Text goes as chat frames, one 128-byte frame at a time; fine for
  2-5 KB pages, slow for long articles. CodeJr's compressed pull
  transfer is the faster road if pages grow.
- One browser, one tab, one caller at a time.
- `extract.js` fixes so far: aria-hidden prices, an opacity-0 decimal
  point, spans styled as boxes, menus burying content. More will come;
  with an LLM in front, fewer of them matter.
