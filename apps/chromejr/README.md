# ChromeJr

A web browser for the PCjr. The browser itself is a real one (Chromium),
hidden on the laptop; the PCjr is its screen and keyboard. The laptop
turns each page into text with every link, button and form field
numbered, and the PCjr works them by number. Because the real browser
holds the session, the cookies and the encryption, modern sites work,
forms and logins included. This is its own app, not part of CodeJr; it
borrows CodeJr's serial link and nothing else.

**State: a prototype (2026-10-09), to find out how hard this is.** The
laptop half exists (`server/cmd/chromejr`, `server/internal/web`). The
PCjr half is for now the CodeJr terminal program pointed at ChromeJr's
port, which gives a scrolling text screen, paging, a status bar and an
input line. A DOS program of its own comes next, if this is worth
going on with.

## Trying it

    just chromejr            start the server (port 4861, hidden browser)
    just chromejr-install    put CHROMEJR.BAT in the PCjr's CHROMEJR folder
    just chromejr-text wikipedia.org     browse from the laptop's terminal

On the PCjr, in CodeJr: `/run`, pick CHROMEJR. CodeJr hangs up, the PCjr
dials ChromeJr, and after `/exit` there it goes back to CodeJr.

    an address        amazon.com   en.wikipedia.org/wiki/IBM_PCjr
    anything else     searches the web for it
    12                follow link 12, or press button <12>
    12 some text      type into field {12} and press Enter
    12= some text     type into field {12} without pressing Enter
    .add to cart      press the first button or link that says so
    m   more of the page      t   from the top      b   back      r   again
    l   list the page's fields and buttons
    w   the whole page: menus, side bars, the search box
    ?   help                  /exit   leave

On the page: `words[12]` is a link, `<12:Go>` a button, `{12:Search____}`
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
