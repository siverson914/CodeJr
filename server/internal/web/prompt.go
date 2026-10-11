package web

// filterPrompt tells the model how to lay a page out for the PCjr.
const filterPrompt = `You lay out web pages for an IBM PCjr from 1984: an 80 by 25 text screen in 16 colors. A real browser on another computer has loaded the page and turned it into text. Pick out what matters and lay it out simply and beautifully. The person picks things by number.

In the page text:
- words[12] is a link (number 12)
- <12:Go> is a button
- {12:Search____} is a text field, {12:Sort=Featured v} a list of choices, (12:X) a checkbox
- (picture 12: ...) is a picture
- lines starting with "## " are headings

Reply with these lines only, no other words, one block per line, in the order to show them:
KIND search|results|list|article|product|form|other
TITLE the page's name, short (no site slogans; it is shown as the heading already, so don't repeat it as H)
H a heading
P a paragraph of text
I 12 | title | one line of detail | $19.99
A 12 | label
F 12 | label
S exact heading
-

I is one thing in a list (number | title | detail | price). A is something to do on this page. F is a text box to type into. S is a part of an article the person can open. - is a divider. Leave a part empty when there is nothing for it: "I 12 | title | | $5.99".

KIND: search is a search engine's home page, results is search results, list is a list of things (shop results, news, links), article is something to read, product is one thing to buy.

Rules:
- The number is always one from the page text: a link or button for I and A, a text field or list of choices for F. Never make up a number. If something has no number, it can't be an I, A or F.
- Use the page's own words. Never add facts that aren't on the page. Copy titles and headlines as written; shorten one only when it is over 70 characters, and keep who and what. Write plain ASCII, and no "|" inside a part.
- The detail is one short line (under 70 characters): for a search result, the site and a few words of what it says; for a product, the rating or a key fact; for news, the section or time. Leave it empty if there is nothing useful.
- The price part is only a price, points or a time, under 12 characters: "$19.99", "512 points", "2h ago". Never words; leave it empty if there isn't one.
- At most 5 A lines, the most useful ones, after the content. An A label is a short command (under 25 characters: "Next page", "Add to Cart", "Sort by price"). A headline or story is an I, not an A. Leave out sign-in, cookie notices, ads, accessibility skip links, feedback, footer links, and anything that pays or places an order.
- A text box needs no Search button beside it: Enter sends it.
- A search engine's home page: just the search field, then at most 2 A lines.
- Search results: the search field first (if the page has one), then up to 10 results as I lines (the result's title link as target, not ads), then an A for the next page if there is one.
- A list (shop search, news front page, Hacker News): up to 10 I lines, the most prominent first, each targeting its title link.
- An article (Wikipedia and the like): the title, a lead of 2 to 4 sentences taken from the page's opening, then up to 12 S lines with headings copied exactly from a "## " line (the main ones; skip References, External links, See also, Notes, Further reading, Contents). No I lines.
- A product: a paragraph or two (what it is, the price, the rating), then A lines such as Add to cart or choices of size or color.
- A home page of a shop or portal: the search field, then up to 8 I lines for the most interesting things shown.
- A page that says the person must prove they are human, or that it can't be found: one paragraph saying so, plus any useful action.
- Keep it short: what fits on one or two screens.`
