# ChromeJr

A web browser for the PCjr. The browser itself is a real one (Chromium),
hidden on the laptop; the PCjr is its screen and keyboard. The laptop
turns each page into text with every link, button and form field
numbered, and the PCjr works them by number. Because the real browser
holds the session, the cookies and the encryption, modern sites work,
forms and logins included. This is its own app, not part of CodeJr; it
borrows CodeJr's serial link and nothing else.

**Next: `HANDOFF.md` here.** **State: a prototype (2026-10-09), to find out how hard this is. Since 2026-10-10 each page goes through an LLM page filter (below), and since 2026-10-11 the PCjr has its own program, `CHROMEJR.EXE`, tried only in the emulator so far.** The
laptop half is `server/cmd/chromejr` and `server/internal/web`; the
PCjr half is `apps/chromejr/chromejr.c`.

## Trying it

    just chromejr            start the server (port 4850, hidden browser)
    just chromejr-install    put CHROMEJR.EXE and CHROMEJR.BAT in the PCjr's CHROMEJR folder
    just chromejr-text wikipedia.org     browse from the laptop's terminal
    just emu-chromejr        CHROMEJR.EXE in the emulator, on local test pages

On the PCjr, in CodeJr: `/run`, pick CHROMEJR. CodeJr hangs up, the PCjr
dials ChromeJr, and after Alt-X there it goes back to CodeJr.

## CHROMEJR.EXE (2026-10-11)

The PCjr's own screen for ChromeJr: the page's title on a blue bar, an
address bar, the page, and a status line with a spinner while the
laptop works. It asks the laptop for Junior markup (`markup=1` in its
HELLO) and draws each view itself: items in yellow with prices in green
at the right, details in cyan, actions as gray buttons side by side,
the search box as a gray field, sections with a small triangle.

    type, Enter       an address (wikipedia.org) or words to search for
    Up, Down, Tab     move between the numbered things on the page
    Enter             open the one picked (or type its number, Enter)
    a box picked      what you type goes into it ("Search:" in the address bar)
    Backspace         back (out of a section or the raw page: back to the view)
    PgUp, PgDn, Home, End    scroll
    Esc               clear what you typed; again: leave the box
    F1 help   F5 load again   Alt-W the page as it is, numbered   Alt-X hang up

It is 26 KB: CodeJr's serial port and link code (`client/serial.c`,
`cjp.c`), CodeJr's way of dialing, copied, and a page of up to 300 rows
in far memory. Words typed while a page is loading wait and go when it
is in; Enter on a pick while loading says to wait, since the thing
picked may not be there on the next page. `/T`, `/X`, `/S` and `/L`
(a log of every frame) are for tests.

The old way still works: CODEJR.EXE dialing port 4850 gets the same
views drawn as text by the laptop.

These commands work from the address bar too (and in CODEJR.EXE):

    an address        amazon.com   en.wikipedia.org/wiki/IBM_PCjr
    anything else     searches the web for it
    3                 pick 3: follow it, press it, or open that section
    3 some text       type into box 3 and press Enter
    3= some text      type into box 3 without pressing Enter
    .add to cart      press the first button or link that says so
    m   more of the page      t   from the top      b   back      r   again
    w   the page itself, every link numbered (w again: menus and side bars too)
    v   the simple view again     f   simple views off and on
    l   list the page's fields and buttons
    ?   help                  /exit   leave

## The page filter (2026-10-10)

Every page goes through a small, fast model on the laptop before the
PCjr sees it (`server/internal/web/filter.go`, the prompt in
`prompt.go`). It reads the numbered text below and picks out what
matters: a search box, ten results or products with their prices, an
article's lead and its sections, at most five actions. The PCjr gets a
short "simple view" numbered 1, 2, 3 instead of a hundred numbers.

- **The page stays the truth.** The model answers in lines like
  `I 12 | title | detail | $19.99`, where 12 is the page's own number.
  Every line is checked against the page: a number that isn't a link or
  button there, a box that is really a password, a section heading the
  page doesn't have, is dropped. So pressing 3 is always a real click on
  a real element. Buttons that spend money (Buy Now, checkout, place
  order) are never in the simple view; `w` shows the raw page with them.
- **The model** is Haiku through `claude -p` (`--model haiku`, which is
  `claude-haiku-5-5` today), on the laptop's Claude subscription: no API
  key. `--effort low` (no thinking: a page took 8 to 11 s with it, 3 to
  4 s without) and lines instead of JSON (half the tokens). It runs with
  no tools, no settings and no CLAUDE.md.
- **Same page, no second call:** views are kept by address and a hash of
  the page's text, so Back and reloading an unchanged page are instant.
- **When it fails** (over 40 s, a reply with nothing usable) the raw page
  is shown, with a line saying why.
- **Junior markup** is what a view is sent as: one block per line, its
  first character the type (`T` title, `H` heading, `P` text,
  `I<n>` item with title, detail and price, `A<n>` action, `F<n>` box,
  `S<n>` section, `-` divider), fields split by tabs. For now the laptop
  draws it into the CodeJr client's text styles; a PCjr program of its
  own would get the markup itself (`markup.go` has the format).

Measured on the laptop (`just chromejr-try`, saved pages, 2026-10-10):

| Page | Choices | Time | Tokens in / out |
|---|---|---|---|
| Google home | search box + 3 | 2.7 s | 1,800 / 50 |
| DuckDuckGo results | box + 11 results | 3.9 s | 3,600 / 390 |
| Wikipedia, IBM PCjr (53 KB) | lead + 16 to 20 sections | 3.3 s | 23,200 / 230 |
| Wikipedia main page | 5 parts + links | 5.0 s | 5,500 / 760 |
| Hacker News | 10 stories | 3.9 s | 4,300 / 500 |
| Amazon home | 8 items + 3 | 3.6 to 6.8 s | 2,600 / 300 to 1,100 |
| Amazon search | 10 products with prices | 4.4 to 6.3 s | 16,300 / 600 to 1,400 |
| Amazon product | text + Add to cart, colors | 3 s | 4,100 / 230 |
| Amazon cart (empty) | text + 1 to 3 | 3 s | 2,000 / 130 |
| BBC News, NPR | 10 to 12 stories | 4 to 9 s | 5,300 to 6,100 / 500 to 2,000 |

The model still thinks sometimes even at low effort (the 6 to 9 s
ones). Cost, as Claude Code reports it, is under a cent a page.

Live in the terminal (`just chromejr-text`): a search typed into
DuckDuckGo's box, result 2 opening Wikipedia's article as a lead and
sections, a section opened and `v` back, Back from the cache in 1.2 s,
an Amazon search and a product. **Google's own search answers the hidden
browser with a robot check**; the home page works, but searches go to
DuckDuckGo (anything typed that isn't an address). Nothing of the
filter has run on the real PCjr or the emulator yet.

    just chromejr-try                    every saved page, as the PCjr would see it
    just chromejr-text -save internal/web/testdata some.site     save more pages
    SHOW_MARKUP=1 SHOW_REPLY=1 just chromejr-try ...   the markup and the model's reply too

On the raw page (`w`, or with `-nofilter`): `words[12]` is a link, `<12:Go>` a button, `{12:Search____}`
a field, `{12:Sort by=Featured v}` a list of choices, `(12:X)` a
checkbox, `(picture 12: ...)` a picture.

## What the prototype showed

Measured on the laptop (`-text`), 2026-10-09:

| Page | Lines of 79 columns | Characters | Time to load and convert |
|---|---|---|---|
| Wikipedia, IBM PCjr (main part) | 1,050 | 54,000 | 1.6 s |
| DuckDuckGo results | 88 | 3,900 | 2 s |
| Hacker News front page | 123 | 5,300 | 1.4 s |
| Amazon search results (main part) | 560 to 660 | 15,700 | 3.2 s |
| Amazon cart (main part) | 17 | 460 | 1.8 s |

- **Amazon works.** Search results with titles, ratings and prices, a
  product page, "Add to cart" pressed and the cart count going to 1, and
  "Buy Now" leading to the sign-in page with its field numbered. No
  robot check appeared in a dozen page loads. Not tried: signing in,
  checkout, a captcha, two-step codes.
- **Converting the page was easier than feared, and is where the work
  is.** A walk over the live page (after its scripts ran) gives readable
  text. Each real site then showed one thing to fix: prices drawn as
  three boxes with an invisible decimal point, text hidden from screen
  readers that people do see, links with boxes inside them, menus that
  bury the content. Expect a steady trickle of these.
- **Main part first.** A page is shown from its main content (`<main>`,
  `article`, a few well-known ids), without menus, headers, footers and
  bars that float over the page. `w` shows everything. Amazon's cart went
  from 265 lines to 17 this way.
- **In the emulator** the PCjr terminal program showed a web search
  typed on the PCjr, with paging. Nothing has run on the real PCjr yet.

## What it would take from here

1. **Speed on the real line.** Pages go as chat text today, one 128-byte
   frame at a time. A 150-line part is about 6 KB. CodeJr's file
   transfer does 2,600 bytes a second with compression; pages should go
   that way, a second or two per part. Unmeasured.
2. **A real PCjr program** (`CHROMEJR.EXE`): the page kept in memory or
   on disk so scrolling is instant, an address bar, arrow keys to move
   between links with Enter to follow, a field editor, no typing of
   numbers. The link, the screen code and the transfer code exist in
   `client/`.
3. **Pictures.** The laptop can cut any picture out of the page
   (`Browser.Shot`), shrink it to 160x200 and dither it to the 16
   colors; the PCjr shows it in its graphics mode. The pieces exist in
   CodeJr's screenshot code, the other way round.
4. **Signing in.** The browser keeps its own profile
   (`~/.local/share/chromejr/profile`). `just chromejr-login` opens it
   as a window on the laptop to sign in to a site once by hand; after
   that the PCjr is signed in too. Passwords typed on the PCjr cross the
   house network unencrypted between the WiFi modem and the laptop, so
   sign in on the laptop.
5. **Buying.** Never type a card number on the PCjr (same reason). Use
   the payment method saved in the account, and add a confirmation
   screen (item, price, address) before any button that places an order.

## Not solved, and may not be

- Sites that insist on a captcha or a phone code each time.
- Pages that are an application more than a document (maps, mail,
  video).
- Anything that needs a mouse position, drag, or hover menus.
