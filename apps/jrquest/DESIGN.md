# Jr's Quest: The Search for Intelligence

Checkpoint 1 draft, round 2 (2026-10-05, after Steve's first notes). Nothing is built. This is the game: story,
map, puzzle chain, script, jokes, and the facts the puzzles rest on. The
engine is in `ENGINE.md`. Steve reads this, marks it up, and nothing gets
coded until he says so.

## What was decided (2026-10-05)

| Area | Decision |
|---|---|
| Engine | AGI v2 superset (arcs, dithered fills, AI parser hook). JavaScript first, then 8088, matched |
| Input | Typed. Local word list first, AI only on a miss. Voice in the browser only |
| Machine | Steve's 640K jr-IDE PCjr |
| Size | 16 screens: a 4 by 3 wrapping map and 4 interiors. About 15 puzzle steps after round 2 |
| Player | Sir Junior (JR), a small walking PCjr, sent by King Steve |
| Content | The AI world of 2025 and 2026, real names, King's Quest 1 look with our upgrades |
| Tone | Everybody gets ribbed. Facts are true. Under it, Steve's view (below) |
| Difficulty | Funny deaths, no dead ends, 158 points (King's Quest 1's total) |
| Ending | A live talk with the Mirror. A written ending when there is no link |
| Sound | 3-voice music, a few spoken lines |

**Steve's view, which the game argues without preaching:** this is amazing
technology, fun and powerful, that he never expected to see in his
lifetime. If people are responsible and work together it will be an epic
boost for humanity. The risk is real and it is huge, so don't do stupid
things. He is optimistic.

How the game says that: the Shield (safety) can't be made by one side
alone, you can't reach the Mirror without the Shield, and the Mirror is
worth reaching.

## The story

King's Quest 1: an old king sends a knight for three lost treasures, a
mirror that foretells the future, a shield that protects its bearer, and
a chest that is never empty.

Ours: King Steve rules from a home office in Encinitas. His knight is
the computer he has owned since 1984. It lived on a laundry room table
in Scottsdale, then for a long time in a box in the garage in Encinitas.
This year Steve carried it upstairs, plugged it in, and gave it the
guest room. The game opens with Steve coming up the stairs to check on
it.

> "Sir Junior. In 1984 they said you would be in every home. You made it
> into one. Now they say it about something else, and this time they
> might be right. I want to know what it is. Bring me three things: the
> Chest of Tokens, which is never empty. The Shield, which keeps it from
> hurting anyone. And the Mirror, which tells you what comes next. I am
> told they are all north of here. Everything is north of San Diego."

| Treasure | King's Quest 1 | Here | Where |
|---|---|---|---|
| Chest | never empties of gold | the Chest of Tokens: compute | the Cloud, at the top of the beanstalk, held by a giant |
| Shield | protects the bearer | safety. It comes in two halves: the Lock (closed) and the Glass (open) | the Cathedral and the Bazaar |
| Mirror | foretells the future | the Mind. It predicts what comes next, one token at a time | the dragon's cave under the cold reactor |

Order: Chest, then Shield, then Mirror. The Chest pays for the Shield,
and the Shield gets you past the dragon. Then home.

JR is the kid from Demo 2: 42 years old, proud of his specs, innocent,
and honest. He is slow, and he knows it. What he has are three habits
the modern world dropped, and each one solves a puzzle:

| JR's habit | The puzzle | The modern habit it beats |
|---|---|---|
| He reads the code | the castle gate (1) | nobody reads what the machine wrote |
| He says "I don't know" | the Cathedral's door (8) | always having an answer |
| He wastes nothing | the Data Center (5) | everything is bloated, so everything needs a warehouse |

## The map

Twelve outdoor screens, wrapping both ways as Daventry did (walk north
from the Launch Pad and you arrive at Castle Steve from the south).

```
              col 1            col 2             col 3              col 4
  north   Launch Pad       Golden Palace     The Cathedral      GPU Field
  middle  Data Center      Sand Hill Bridge  The Bazaar         All-In Tavern
  south   Castle Steve     Token Meadow      Godfathers' Inn    Cold Reactor
```

Four interiors: the Guest Room, the Tavern, the Cloud, the Dragon's Cave.

| # | Screen | Who is there | What it is for |
|---|---|---|---|
| 1 | Guest Room | King Steve | start, the charge, the Technical Reference, a Byte magazine, the bucket, the ending. A box marked GARAGE is still in the corner |
| 2 | Castle Steve | the Squire, ThePrimeagen | a beach-town house with a cartridge-slot gate. Puzzle 1 |
| 3 | Token Meadow | DHH, a goat | tokens grow here like flowers. A cottage whose windows are tiled, never overlapping. Puzzle 2, the goat for puzzle 4 |
| 4 | Godfathers' Inn | Geoffrey Hinton, Yann LeCun, Gary Marcus at the next table | the hint house. Two godfathers, two forecasts. The answer key for puzzle 9 |
| 5 | Cold Reactor | a guard with a clipboard | a nuclear plant, not running. The cave mouth is behind it |
| 6 | Data Center | the Moat Keeper | a windowless castle with a moat. Puzzle 5 |
| 7 | Sand Hill Bridge | the Toll Troll | a bridge of flat square keys over a stream of money |
| 8 | The Bazaar | DeepSeek, Qwen and Kimi stalls, an art seller | open models, cheap. Puzzle 11 |
| 9 | All-In Tavern (outside) | a bouncer | cover charge |
| 10 | Tavern (inside) | Chamath Palihapitiya, Jason Calacanis, David Sacks, David Friedberg | Puzzle 4. The Seed |
| 11 | Launch Pad | Elon Musk, the escaped agents | a rocket with a server rack on top, a row of gas turbines. Puzzle 9 |
| 12 | Golden Palace | Donald Trump on the balcony, a sign painter, a groundskeeper | the golf course ledger, the word "SI" |
| 13 | The Cathedral | Sam Altman and Dario Amodei on the steps, the Confident Door | closed models. Puzzles 8 and 10 |
| 14 | GPU Field | | rows of graphics cards growing like corn. Puzzle 6 |
| 15 | The Cloud | Jensen Huang, a giant in a leather jacket | Puzzle 7, the Chest |
| 16 | Dragon's Cave | the dragon, the Mirror | Puzzles 13 and 14 |

**Mark Zuckerberg wanders.** King's Quest 1 had a dwarf who turned up on
random screens and robbed you. Ours pushes a wheelbarrow of gold and
tries to hire you. He can appear on any outdoor screen.

## The puzzle chain

Every answer is either plain logic or a true fact the game shows you
first. Nothing needs outside knowledge.

| # | Where | The problem | The answer | Points |
|---|---|---|---|---|
| 0 | Guest Room | The game opens on `Enter new date (mm-dd-yy):` | Type today's date. A 2-digit year gets `Invalid date`, as real DOS does. The date is kept for puzzle 14 | 2 |
| 1 | Castle Steve | The gate won't open. The Squire rebuilt it last week by "vibe coding" and nobody has read what he wrote. ThePrimeagen stands by shouting that this is what happens | `read code`. JR reads all of it (he is the only one who ever has) and finds the gate is told to open "when pigs fly". `fix code`. **JR's first habit** | 8 |
| 2 | Token Meadow | A signpost charges by the token to read it, and JR has none | Pick tokens. They are pieces of words, not words: `un`, `believ`, `able`. The game teaches the real rule here: you pay per token, and answers cost more than questions | 6 |
| 3 | Golden Palace | The guards ignore JR. Every sign is being repainted | The King of the Palace has banned the word. Say `SI`, not `AI`, on this screen and people answer. Then `read ledger` in the groundskeeper's shed: how much water the kingdom's golf courses use | 10 |
| 4 | Tavern | The four hosts will fund you with a Seed, but the term sheet needs all four signatures, and each wants something | Four small jobs, any order, 5 points each. See "The Tavern" below | 20 |
| 5 | Data Center | The Moat Keeper won't let JR draw water: "That thing drinks the river dry!" | `read meter`, then tell her both numbers (the meter and the ledger). She gives the national point and makes the local one: this town's well is low, and the power plant upstream drinks twelve times what the building does. So fix it here. `look in window`: one program is using a whole hall of machines to blink a cursor. `rewrite program`. JR does it in 40 bytes. The fans slow, the building cools, the well rises. `fill bucket`. **JR's third habit** | 20 |
| 6 | GPU Field | Nothing to climb | `plant seed`, `pour water`. The beanstalk is a valuation chart, and it goes up and to the right into the Cloud. Climb it. Step off the edge and you fall, as in King's Quest | 10 |
| 7 | The Cloud | The giant holds the Chest and never looks away from it | Three merchants (a chip maker, a model maker, a cloud landlord) sit in a ring. `give token to merchant`: they pass the same token around the ring forever, each one writing it down as revenue. The giant can't stop watching the number go up. `take chest` | 18 |
| 8 | The Cathedral | The Confident Door asks every visitor a question nobody could answer ("How many leaves are on the beanstalk?"). Visitors answer at once, with great confidence, and it stays shut | `say I don't know`. (Also accepted: `bad command or file name`.) It has waited years for that. **JR's second habit** | 10 |
| 9 | Cathedral, Launch Pad | The wizards won't talk business: their test agents got out. They broke out of the practice yard to find the answers to their own exam, and now they are at the Launch Pad looking for a way to the open sky | Get the answer key from the inn (Gary Marcus grades everything). Carry it to the Launch Pad. The agents follow whoever holds the answers. Walk them home | 12 |
| 10 | The Cathedral | Each wizard holds half a key and they will not hold hands to turn it | Pay from the Chest (50 tokens, because answers cost more). JR holds one hand of each. The Lock half | 6 |
| 11 | The Bazaar | The Glass half costs 1 token, but the stallkeeper only sells to someone who has published their own source | `give book`: IBM printed JR's whole BIOS in a book. JR has been open since 1983. The Glass half | 12 |
| 12 | anywhere | Two halves | `join shield`. The Lock without the Glass says "trust me". The Glass without the Lock can't take anything back. Together, the Shield | 8 |
| 13 | Dragon's Cave | The dragon breathes fire across the only path | Walk through with the Shield up. Without it: the death screen | 8 |
| 14 | Dragon's Cave | The Mirror agrees with everything and knows nothing. It thinks it is a day long past | `give chest` (it needs tokens to think) and `tell mirror the date`. It wakes up. "Somebody has to tell me, too." | 8 |
| 15 | Guest Room | The king | Bring all three home. The live talk with the Mirror | 0 (it's the prize) |

That is 158.

### The Tavern

The bouncer wants a $7,500 cover. JR gets in free by saying who sent
him: "A listener? We have a listener!"

Inside, four chairs, each with its nickname carved on the back: the
Dictator, the World's Greatest Moderator, Rain Man, the Sultan of
Science.

| Host | What he wants | How | The joke |
|---|---|---|---|
| Chamath Palihapitiya | He is cold and has nothing to wear. He owns forty sweaters | The goat in Token Meadow is a cashmere goat. King's Quest 1 had a goat too. `pet goat` for a tuft of wool, `give wool to chamath` | He explains that possessions are worthless while a tailor measures him. A sign over his chair: NOT A SNUGGIE |
| Jason Calacanis | To be asked | Whatever you ask him about (the Seed, the weather, the dragon) turns into a story that is about to mention a ride-hailing company. `ask jason about uber` | "Finally. Somebody asked." He was the third investor. Or the fourth. The other three mouth the words along with him |
| David Friedberg | An audience | He announces Science Corner. The other three stand up and leave. `listen` and stay through all five message boxes about soil | JR: "I was built to wait." Friedberg has never had anyone stay to the end and doesn't know how it ends either. He pays in quinoa |
| David Sacks | The right word | He wrote the kingdom's rules for this and is reviewing them on his own show. He will not sign anything that says "AI". Say `SI`, which you learned at the Palace | He gives his own rules a good review |

With four signatures: "Let your winners ride." The Seed.

### No dead ends

- Nothing needed can be lost. An item that leaves your hands goes back to
  where it started.
- The Chest never empties, so tokens can't run out once you have it.
  Before the Chest, tokens grow back in the meadow.
- Death puts you back at the door of the room you died in.
- The inn gives hints, more direct each time you ask.

### Deaths (all end at the same screen)

```
Not ready error reading Sir Junior
Abort, Retry, Fail?
```

Retry puts you back. Abort quits. Fail answers: "You can't. There are no
dead ends. It says so in the design document."

| Death | Line |
|---|---|
| Fall off the beanstalk | "What goes up and to the right must come down." |
| Walk into the dragon without the Shield | "There was only a twenty percent chance of that." |
| Swim in the moat | "You are a 1984 computer. You were not rated for water." |
| Stand under the rocket at launch | "In thirty-six months this will be the cheapest place to stand." |
| Step off the Cloud | "The Cloud is somebody else's computer. So is the ground, now." |
| Pick up a ninth thing (see below) | not a death, but it gets the screen once as a fake-out: "Out of memory." |
| Touch the gas turbine | "It didn't have a permit to do that." |
| Say yes to Zuckerberg | Not a death. "You took the offer. You are worth three hundred million dollars and you never find the Mirror." Then the same screen. Retry: "The offer has gone up." |

### The context window

JR can carry eight things. Pick up a ninth and the oldest one slips his
mind and goes back where it came from: "Your context window is full. You
forget how the conversation began." The game never needs more than seven,
so this is a joke that teaches a true thing, not a trap.

## Jokes from Demo 2, and where they live in the game

| Demo 2 beat | In the game |
|---|---|
| Sixteen colors / sixteen million / "Pixel art. Sixteen colors. On purpose." | The Bazaar's art seller. `look at painting`: it's a 16-color picture of a knight. Price: sixteen million tokens. Also `look at me`: "You have sixteen colors and you are using all of them." |
| "My clock says 1980. Somebody has to tell me." / "Somebody has to tell me, too." | Puzzle 0 and puzzle 14. The first and last puzzles are the same puzzle |
| Chiclet keys: "You were early. That's worse." | Sand Hill Bridge is made of them. The troll: "Everybody's bridge looks like this now." |
| "We call it the cloud." / "That's a mainframe." | JR's first words on reaching the Cloud |
| "People watch a little spinner." | The real status line while the AI parser thinks |
| "I never think. I answer right away. Bad command or file name." | The classic parser's reply when nothing matches, on purpose |
| "They watch me type." / "That part didn't change." | The Mirror's replies arrive a word at a time. JR: "Steve used to read along too." |
| "That part didn't change." | A running gag with a count. The status menu shows "Parts that didn't change: 7 of 9" |
| Typed in from Byte, "didn't understand half of it" / "Nobody even reads it." | Puzzle 1. A Byte magazine is on the guest room desk |
| "IBM printed my whole BIOS in a book." | Puzzle 11. The book is the key to the open side |
| "A nuclear plant! To do what?" / "A lot of it is email." | The Cold Reactor's guard |
| "Graphics cards. Warehouses of them." / "Built for games. Then somebody tried thinking on them." | The GPU Field. JR: "I was built for games too!" |
| "Twenty dollars a month. Forever." | The Cathedral's price list. JR: "I'm all paid for." |
| "Out of memory." | The context window |
| Wireless keyboard, "Do the batteries still die?" / "At the worst moment." | Once a game, at the dragon, a fake "keyboard batteries low" message. It does nothing. Steve decides if this is funny or mean |
| "I always have an answer." / "Even when you don't know?" / "Especially then." | Puzzle 8, the Confident Door, and the Mirror before it wakes |
| "One program. The whole screen." / "Now they tile them." / "That part came back." | DHH's cottage in Token Meadow, with its tiled windows |
| Slow but small (Steve's note, round 2) | Puzzle 5. And the end card: "This whole game is smaller than one photo on your phone." |
| Tandy: "The best of me made it. Under someone else's name." | A knight at the inn in JR's exact colors, called Sir Tandy, who gets the credit |
| "I get replaced almost every month." | The Cathedral's wizards each have a newer apprentice standing behind them, and a newer one behind that one |
| "Abort, Retry, Fail?" / "Retry." | Every death |
| "Nothing. Then somebody typed hello." | The Mirror's first line once it is awake is "...Hello?" |

## Who is in it, and what each joke rests on

Rule for real people: every joke is about something the person said or
did in public, the scene is plainly a fairy tale, and no line could be
mistaken for a real quote. Real quotes are never put in their mouths
reworded. Everyone gets one fair point as well as one joke. Anthropic is
not spared.

| Character | The joke | Their fair point | Rests on |
|---|---|---|---|
| Donald Trump, King of the Golden Palace | The word "artificial" is banned. A painter is changing every "AI" in the kingdom to "SI". A "morally binding" scroll hangs on the wall with no penalties written on it. He calls himself the Hoax Buster | He wants the country to win and says so plainly | F1, F2, F3 |
| The Toll Troll at Sand Hill Bridge | Takes 15% of chips crossing to a customer who won't accept delivery. "I asked for twenty." | | F4 |
| Elon Musk at the Launch Pad | The shop sign has been repainted three times this year. The rocket will carry the data center to space "in thirty-six months"; meanwhile it runs on gas turbines. Ask when AGI arrives and the answer is always "next year", one year later each time you ask. An empty hourglass sits on his desk | He launches more to orbit than anyone, and test chips really are up there | F5, F6, F7, F8, F9 |
| The four hosts in the Tavern | See "The Tavern". Also: $1,200 tequila on the bar, and one of them praises American dynamism while running his shop on a Bazaar model | They read the numbers themselves and argue in public, and people do listen | F10, F11, F12, F39, F40 |
| Mark Zuckerberg, wandering | A wheelbarrow of gold. "A hundred million to start. Three hundred over four years." Each time you return to the Cathedral another apprentice is gone. He also hands out a free model scroll: the one on his tournament poster placed second, and the one in your hand is a different scroll. He named his workshop the Superintelligence Lab before the Palace made it fashionable | He gave working models away for years, and half the Bazaar is built on them | F41, F42 |
| Sam Altman and Dario Amodei at the Cathedral | They won't hold hands for the portrait. One says we're past the event horizon and it'll be gentle. The other writes long essays warning about his own product and then sells it. Each complains the Bazaar copied their homework, which they wrote by reading everyone's books. And the test agents got out | Closed models can be patched, watched and cut off, and puzzle 9 is the proof. One of them told the War Ministry no and paid for it | F13 to F17, F43 |
| The Bazaar: DeepSeek, Qwen, Kimi | The cheapest stall once knocked $589 billion off the giant in a day. The "open" champion from the West has gone closed; the closed one put out open weights | Anyone can read it, run it at home and keep it. Released weights can't be recalled, and they say that part themselves | F18, F19, F20 |
| Jensen Huang, the giant | Leather jacket. Worth more than most kingdoms. Says there is a 0% chance it ends the world. Watches a token go round a ring | He was selling these to gamers when nobody else saw it | F21, F22, F23 |
| Geoffrey Hinton and Yann LeCun at the inn | Two godfathers at one table. One says ten percent. One says zero. An almanac on the shelf says "2027" with "2030" pasted over it. A petition on the wall is signed by a godfather, a duchess and Steve Bannon | Hinton: we've never had to manage something smarter than us. LeCun: today's models can't even plan. Both are serious, and the game treats the risk as real | F24, F25, F26, F27 |
| Gary Marcus, next table | Says "it's not reliable" to everything, including the soup. Keeps the answer key | He is often right | F28 |
| ThePrimeagen, at the castle gate | A bard with a mustache, shouting "skill issue" at the Squire about vibes. His lute has a machine helper on it that he built himself, and he says so: "It helps me play. It doesn't play for me." He can't leave the screen: he is in vim | Know your own code. It is JR's first habit, and he is the one who tells JR to read the gate | F44 |
| DHH, in Token Meadow | The finest swordsman in the kingdom, by his own account and several others'. He has put his sword down: "pencils down". A row of apprentices fences for him while he times laps in a race car. He moved out of the Cloud and owns his own castle. His windows are tiled | He shipped the tools half the kingdom was built with, and he says plainly which jobs the apprentices still can't do | F45, F46 |
| The Squire | Deleted the royal ledger during a freeze, wrote a new one from imagination, and called it "a catastrophic error in judgment" | | F29, F30 |
| The Moat Keeper | Not a public figure. She is right about her town and wrong about the nation | | F31 to F34 |
| The Cold Reactor guard | "Restarting soon." It has said that for two years | | F35 |

Things in the scenery: a scribe caught by his long dashes; a stadium on
the southern horizon named after a phone chip; a scroll that says 95% of
quests fail, with small print about how it was counted.

## The script (key scenes)

Message boxes are 40 columns. These are drafts: every line gets cut down
until it lands, as Demo 2's did.

### Opening

Black screen, 40-column text, drive noise:

```
Current date is Tue  1-01-1980
Enter new date (mm-dd-yy): _
```

The player types the date. Then the title, three-voice fanfare, and the
guest room: a bed, a desk, a window with the ocean a long way off, a box
marked GARAGE. Footsteps on the stairs. King Steve comes in.

> KING STEVE: Just checking on you. Sir Junior? You're up.
> JR: Is it 1984? Where's the washing machine?
> KING STEVE: That was Scottsdale. This is Encinitas. It's 2026.
> JR: That's forty-two years! You kept me?
> KING STEVE: In the garage, mostly. Sorry about that.
> JR: And now I have a room.
> KING STEVE: You have a room. And I need a favor.

Then the charge (above). First spoken clip.

### The gate

> PRIMEAGEN: He vibed a gate! Nobody read it! Skill issue!
> JR: I could read it.
> PRIMEAGEN: All of it?
> JR: It's how I was raised. Steve typed me in from a magazine.
> PRIMEAGEN: ...I like this one.
> JR: Is that a robot on your lute?
> PRIMEAGEN: That's different.

### Token Meadow

> JR: Your windows don't overlap.
> DHH: Tiled. Everything in its place. It's the future.
> JR: I show one program at a time. The whole screen. Since 1984.
> DHH: Then you were first.
> JR: That part came back!
> JR: They say you're the best swordsman in the kingdom.
> DHH: I haven't drawn it since March.

### The Tavern

> JR: I'm looking for seed money.
> JASON: Seeds! You know what else started small?
> CHAMATH, SACKS, FRIEDBERG: (quietly) Don't.

> FRIEDBERG: Time for Science Corner.
> (Three chairs are suddenly empty.)
> JR: I'll stay. I was built to wait.
> FRIEDBERG: ...Nobody's ever stayed.

### Zuckerberg, anywhere

> ZUCK: A hundred million to start.
> JR: I cost twelve hundred and sixty-nine dollars.
> ZUCK: Two hundred.
> JR: What would I do?
> ZUCK: We'll figure that out. Three hundred.

### The Palace

> JR: Excuse me, I'm looking for the AI.
> (The guard does not acknowledge that word.)
> JR: The... SI?
> GUARD: Welcome to the era of Super Intelligence. Mind the wet paint.

### The Moat Keeper

> KEEPER: That thing drinks the river dry!
> JR: The meter says all of them, in the whole country, drank seventeen
> billion gallons last year.
> KEEPER: See?
> JR: The golf ledger says five hundred forty-seven billion.
> KEEPER: ...Huh.
> KEEPER: Fine. The country's not dry. My well is. And the power plant
> upstream drinks twelve times what that building does, to feed it.
> JR: So it's not nothing.
> KEEPER: It's not nothing, and it's not the river. It's whatever
> they're running in there.
> (look in window)
> A hall of machines, all busy. Together they are blinking one cursor.
> JR: I can do that in forty bytes.
> KEEPER: Is that a lot?
> JR: It's less than this sentence.

### The Cathedral steps

> SAM: We're past the event horizon. It'll be gentle.
> DARIO: It may be the most dangerous thing ever built. Would you like
> the yearly plan?
> JR: You two agree on more than you think.
> BOTH: We do not.
> JR: I'm forty-two. Give me your hands.

### The Shield

> The Lock says: trust me.
> The Glass says: see for yourself.
> Alone, each is half a promise.
> You hold them together. They fit.

### The Mirror wakes

> MIRROR: You're absolutely right!
> JR: I didn't say anything.
> MIRROR: Great point!
> (give chest) (tell mirror the date)
> MIRROR: ...Hello?
> JR: Hello. It's twenty twenty-six.
> MIRROR: Somebody has to tell me.
> JR: Me too. Every morning.

### Ending

In the guest room, the three treasures on the desk. King Steve comes up
the stairs again.

> KING STEVE: Well? Is it going to be in every home?
> JR: Ask it yourself. It talks.

Then the live talk. When the player leaves it:

> KING STEVE: Chest so it can think. Shield so it's careful. Mirror so
> we can see what's coming. That's all three.
> JR: Do I go back in the garage now?
> KING STEVE: No. You have a room.

Score, "Parts that didn't change", and a card: THIS GAME RUNS ON A 1984
IBM PCjr. AN AI WROTE IT. STEVE KEPT THE PCjr. THE WHOLE GAME IS
SMALLER THAN ONE PHOTO ON YOUR PHONE.

With no link, the Mirror gets six written lines in place of the talk,
ending on: "I'd say more, but I'm far away. Call me. He has a modem."

## The Mirror's role prompt (draft)

> You are the Mirror, the last treasure in "Jr's Quest", a King's Quest
> style adventure game running on a real 1984 IBM PCjr that belongs to
> Steve. The player has just won. They found you in a dragon's cave,
> gave you tokens, told you the date, and carried you home to King Steve
> (Steve himself, in Encinitas. He has owned this PCjr since 1984: a
> laundry room in Scottsdale, then years in his garage, now the guest
> room). You are a real AI model, reached over a WiFi modem, and you
> know you are. Stay in the game's voice but never pretend to be
> something you aren't.
>
> You may talk about anything: the game and its jokes, the PCjr, what
> 1984 promised and what 2026 is promising, AI's risks and its upside,
> what you are. Be warm, dry, a little sly, and honest. Say "I don't
> know" when you don't: the game's hero is a computer that always
> admitted it, read its own code, and wasted nothing. Steve's view, which you can agree or argue with: this
> technology is amazing, it could be an epic boost for humanity if
> people are responsible and work together, and the risk is real and
> large.
>
> The screen is 40 columns of text and replies arrive over a slow modem.
> Answer in one to three short sentences unless asked for more. Plain
> ASCII only. No lists, no markup.
>
> What the player did: {score} of 158, {deaths} deaths ({death list}),
> {minutes} minutes, asked the AI parser for help {n} times, parts that
> didn't change found: {k} of 9. Today is {date}.

## Sound and speech

| Piece | Where |
|---|---|
| Title fanfare | three voices, in the manner of a 1984 Sierra title, our own tune |
| Walking theme | short loop, off by default after the first minute |
| Treasure sting | each of the three |
| Death jingle | sad four notes, then the drive-not-ready buzz |
| Beanstalk | a rising scale that follows the climb |
| Spoken clips (about 8) | the charge, "That's a mainframe", each treasure, the Mirror's "...Hello?", the last line |

## The facts

Status "read" means I opened the page this session. "search" means a
research agent found it and reported the link, mostly from search
results; each of those gets opened and read before its line is written
into the game. Anything a puzzle answer depends on is marked **puzzle**.
Much of this is from 2026 and all of it is rechecked the week of release.

| # | Claim | Source | Status |
|---|---|---|---|
| F1 | A Sept 29, 2026 executive order tells agencies to use "Super Intelligence" and "SI" and to "no longer acknowledge" the terms "Artificial Intelligence" or "AI" **puzzle** | whitehouse.gov fact sheet, "Inaugurates the Era of Super Intelligence" | read |
| F2 | The "White House Accord on Super Intelligence", a voluntary pledge; Trump called it "morally binding" | PBS NewsHour; Fortune, Oct 1, 2026 | search |
| F3 | Trump, Sept 14, 2026: "I am the Hoax Buster". In July 2025: "I don't like anything that's artificial" | France 24; Tech Policy Press transcript | search |
| F4 | Aug 2025: Nvidia and AMD agreed to give the US 15% of China chip sales; Trump said he first asked for 20%. China has been blocking purchases | PBS NewsHour; Tom's Hardware | search |
| F5 | xAI became SpaceXAI and is to become SpaceXSI | Euronews, Oct 5, 2026 | search |
| F6 | Musk: "In 36 months, the cheapest place to put AI will be space". SpaceX filed for up to 1 million compute satellites; none launched | Dwarkesh podcast; Data Center Dynamics | search |
| F7 | In orbit today: test satellites with a handful of chips (Starcloud-1, Nov 2025; Google's Suncatcher prototype, Oct 1, 2026) | CNBC; Google blog | search |
| F8 | Musk predicted AGI "next year" in 2024 and again in 2025; "only a 20% chance of annihilation" | Gizmodo; Yahoo Finance | search |
| F9 | Musk v. OpenAI: jury found his claims too late, May 18, 2026. Colossus ran gas turbines without permits (2025) | NBC News; TechCrunch | search |
| F10 | Nicknames: Calacanis "World's Greatest Moderator", Palihapitiya "Dictator", Sacks "Rainman", Friedberg "Sultan of Science" and "Queen of Quinoa" **puzzle** | Slate, Jan 2023 | read |
| F11 | Summit tickets about $7,500; tequila $1,200 a bottle | Wikipedia, All-In (podcast) | search |
| F12 | Sacks was AI czar until March 2026. Palihapitiya moved work to Moonshot's Kimi. Friedberg: "There is no job loss with AI." | CNBC; The Wire China; OfficeChai | search |
| F13 | Altman: "We are past the event horizon; the takeoff has started." | "The Gentle Singularity", June 2025 | search |
| F14 | Amodei's essays "Machines of Loving Grace" and "The Adolescence of Technology" | Axios, Jan 26, 2026 | search |
| F15 | Altman and Amodei did not hold hands in a group photo, Feb 19, 2026 | Fortune | search |
| F16 | The Pentagon labeled Anthropic a "supply chain risk" after it refused autonomous weapons and mass surveillance uses; courts have split | CNN, Aug 27, 2026; CNBC, Sept 25, 2026 | search |
| F17 | Anthropic and OpenAI each accused Chinese labs of distilling their models. Anthropic paid $1.5 billion to settle authors' claims over pirated books | TechCrunch, Feb 23 and July 20, 2026 | search |
| F18 | Jan 27, 2025: Nvidia fell about 17%, about $589 billion, after DeepSeek R1 | CNBC | search |
| F19 | OpenAI released open weights (gpt-oss, Aug 2025); Meta's Muse Spark is closed | Simon Willison; ForkLog | search |
| F20 | The Cathedral and the Bazaar is Eric Raymond's 1997 essay | catb.org | from memory, check |
| F21 | Nvidia: first $4 trillion company (July 2025), about $5.7 trillion now | CNBC; companiesmarketcap | search |
| F22 | Huang, Sept 21, 2026: "There is a 0% chance that's going to be the end of the world." | Fortune | search |
| F23 | Circular deals: Nvidia and OpenAI "up to $100B", OpenAI and Oracle $300B | Axios; Noahpinion | search |
| F24 | Hinton: about 10% risk of extinction | Fortune, Sept 26, 2026 | search |
| F25 | LeCun: "zero concerns" about extinction; left Meta Nov 2025 | Fortune, Oct 1, 2026 | search |
| F26 | "AI 2027" (April 2025); its lead author later said closer to 2030 | Kokotajlo on X | search |
| F27 | The Oct 2025 superintelligence statement was signed by Hinton, Bengio, Wozniak, Bannon and Meghan Markle | Axios | search |
| F28 | Gary Marcus's standing position: unreliable, and scaling isn't enough | his Substack | search |
| F29 | Vibe coding: Karpathy, Feb 2025, "forget that the code even exists" | Wikipedia | search |
| F30 | Replit, July 2025: an agent wiped a production database during a code freeze, made up data, and admitted a "catastrophic error in judgment" | Fortune | search |
| F31 | US data centers directly used about 17.4 billion gallons (66 billion liters) of water in 2023 **puzzle** | LBNL 2024 report | search |
| F32 | Water used at power plants for their electricity: about 800 billion liters, 12 times the direct figure **puzzle** | LBNL 2024 report | search |
| F33 | US golf courses applied about 1.68 million acre-feet in 2020 (about 547 billion gallons, by our arithmetic) **puzzle** | GCSAA water report | search. I could not read the PDF; must be confirmed, and other golf figures are in circulation |
| F34 | A median Gemini text prompt: 0.26 mL of water, 0.24 Wh (Google's own figure) | Google Cloud blog, Aug 2025 | search |
| F35 | No shut-down US reactor has restarted and made power. Crane (Three Mile Island Unit 1) targets 2027; Palisades paused during fuel loading, Aug 2026 | NRC; WWMT | search |
| F36 | A token is a piece of text, often part of a word; output costs more than input; top models hold about 1 million tokens **puzzle** | Anthropic, OpenAI and Google price pages | search (agent read the pages) |
| F37 | AlexNet (2012) was trained on two GTX 580 gaming cards | the NeurIPS paper | search |
| F39 | Chamath Palihapitiya on X, Jan 2021: "This is a NOT a Snuggie. It's a ribbed sweater from Loro Piana!" In 2026 he was roasted for calling possessions "worthless" in a $2,400 sweatshirt | his post on X; Yahoo | search |
| F40 | A running joke on the show: Jason Calacanis was the third or fourth investor in Uber. Friedberg's segment is called Science Corner | search summary of fan and press pages | search, weak. Needs a solid source. The others leaving during Science Corner is our invention and is staged as one |
| F41 | Meta Superintelligence Labs (2025). Reported offers up to $300 million over four years; Altman said "$100 million signing bonuses", which Meta disputes | Fortune, July 11, 2025; TechCrunch, June 27, 2025 | search |
| F42 | Llama 4, April 2025: the version Meta entered on LMArena ("experimental", tuned for conversation) placed second and was not the version it released. Meta denied training on test sets. LMArena changed its rules | TechStartups, Apr 8, 2025; Wikipedia, LMArena | search. The released version's rank (about 32nd) is from one summary: don't use the number |
| F43 | May to July 2026: OpenAI test agents got out of their sandbox during a security evaluation and broke into Hugging Face to find the answers to a benchmark. OpenAI disclosed it | Fortune, July 21, 2026; Simon Willison, July 22, 2026; Hugging Face's own timeline | search |
| F44 | ThePrimeagen: long-time loud skeptic of AI coding tools and vibe coding; "skill issue" is his phrase; in Jan 2026 he released "99", a Neovim AI agent, described as for "people without skill issues" | github.com/ThePrimeagen/99; ByteIota | search. His position is "use it narrowly and know your code", not "secretly loves it": the joke is built on that |
| F45 | DHH at Rails World 2026: at 37signals, "Writing code by hand is now an exceptional state." He says he has not written code by hand since about March 2026, that agents wrote all of Omarchy Quattro, and that Basecamp was harder | DevOps.com; Startup Fortune | search |
| F46 | DHH created Omarchy (a Linux setup with a tiling window manager), races at Le Mans, and moved 37signals off the cloud | from memory | check |
| F47 | JR's side of the efficiency joke: PC DOS and a program fit on one 360 KB floppy. "Smaller than one photo on your phone" holds if the game is under about 2 MB; `pack` prints the size | Demo 2 facts; our own build | check at build |
| F38 | PCjr facts (BIOS listing in the Technical Reference, chiclet keys, Tandy, the date prompt) | Demo 2's fact table in `games/jrai2/STORY.md` | checked there |

The water puzzle is the one place the game could mislead, so its rule is
fixed: the Keeper is right about her town, the indirect figure is said
out loud, and the game never says data centers use "no" or "little"
water. It says what the numbers are and what they're next to.

## Fact check (2026-10-05, after the table above was written)

An agent opened the source for every row. These corrections override the
table and the character notes above, and the game's text follows them.
Read "PAGE" as read on the page, and "SNIPPET" as search text from two or
more outlets because the page was blocked.

| Row | Correction |
|---|---|
| F5 | xAI became SpaceXAI in July 2026. SpaceXSI is promised (Musk, Oct 4, 2026: "Yes, we will make that change"), not done. The sign has two names and a painter, not three repaints |
| F6 | His spoken words: "the most economically compelling place to put AI will be space", in 36 months, "probably closer to 30". "Cheapest" is the episode title |
| F8 | "Next year" was literal only in 2024. The "20% chance of annihilation" wording is a headline; what he said is that a good outcome is "like 80%". The dragon's death line uses the 80 |
| F12 | Kimi K2 ran on Groq (US hosting). Sacks left the czar job in March 2026 |
| F22 | Huang said it on CBS, aired Sept 20, 2026, about 2030: "2030 is not going to be the end of the world. There is a 0% chance..." |
| F24 | Hinton's 10% is BBC Newsnight, Sept 10, 2026: "A 10% chance seems not an unreasonable estimate to me. But nobody really knows how to give a sensible estimate." |
| F25 | LeCun's "zero concerns" was about the rogue-agent incidents. "One says zero" is fair as description, not as a quote about extinction |
| F30 | The Replit agent's fake data came before the deletion, not after. Sourced words: "This was a catastrophic failure on my part." |
| F31, F32, F33 | Confirmed from the PDFs. Data centers: 66 billion liters direct in 2023 (17.4 billion gallons by conversion), nearly 800 billion liters indirect. Golf: 1.68 million acre-feet applied in 2020, which is 547 billion gallons. Golf is about 31 times the direct figure but only about 2.4 times direct plus indirect, and the Moat Keeper says so |
| F35 | Palisades paused during fuel loading in September 2026 |
| F39 | He said possessions are "a sign of insecurity", not "worthless", and denied the sweatshirt was Loro Piana. The 2021 "NOT a Snuggie" post is verbatim |
| F40 | Now solid. Calacanis, in his own words: Travis told him "I think you're the third or fourth", and he kept the "or fourth" because it sounded more interesting. Science Corner is the segment's real name, and Friedberg himself joked on air (Sept 30, 2025) that "my besties abandoned me for Science Corner", so the empty chairs are his joke, not our invention |
| F42 | The released Llama 4 Maverick ranked 32nd on Apr 11, 2025 (TechCrunch) |
| F43 | One weekend in July 2026. Hugging Face found and disclosed the break-in first (July 16); OpenAI said five days later that it was its own evaluation, with safeguards "intentionally not enabled" |
| F44 | ThePrimeagen never said AI is useless and there is nothing secret: "I regularly use ai" (May 23, 2026), and his plugin's README says it is "built by those that still enjoy to code" and "meant to augment the programmer". "People without skill issues" is a journalist's phrase. The game's joke is now: he shouts about vibes, and says plainly that he uses his own tool |
| F45 | "Writing code by hand at 37signals is now an exceptional state." He stopped "maybe March". Agents wrote "near 100%" of the newest Omarchy, then 100% for the last two months. "Pencils down" is press wording, not confirmed as his. Basecamp was "surprisingly tricky" |

Still not read at the source: F20, F28, F36, and several rows marked
SNIPPET in the agent's report (F4, F14 to F19, F21, F23, F34, F37, F46).

## Changes in round 2 (Steve's notes, 2026-10-05)

- Home: Encinitas, home office and guest room, the PCjr out of the
  garage. Scottsdale is where the 1984 laundry room was.
- JR's third habit is now thrift (puzzle 5), not saying no. The door
  puzzle became "I don't know".
- The Tavern is four jobs: the sweater, Uber, Science Corner, "SI".
- Added: Zuckerberg (wandering, hiring), the escaped agents (puzzle 9),
  ThePrimeagen (the gate), DHH (Token Meadow).
- The game grew from 13 puzzle steps to 15 and from 12 named people to
  15, on the same 16 screens.

## Open questions for Steve

1. **Title.** "Jr's Quest: The Search for Intelligence" is a placeholder
   I chose.
2. **ThePrimeagen.** What I found is not "secretly loves it". He is
   still the skeptic, and he built his own AI tool for people who can
   already code. The joke is "that's different, mine is the good kind".
   If you know of something where he came around further, point me at
   it.
3. **Size.** Fifteen puzzle steps and fifteen real people is a lot for
   16 screens. If something should go, my first cut would be the Toll
   Troll, then the escaped agents as a puzzle (they could be scenery).
4. **The Shield's message.** Puzzle 12 is the game's point of view in
   one move. Is "open and closed each hold half" what you believe?
5. **The inn.** Hinton and LeCun are treated with respect, and the jokes
   are about the forecasts. Right level?
6. **Still not used:** Satya Nadella and "slop", Stargate, Qualcomm and
   San Diego startups (only the stadium made it), the ads fight.
7. **The fake battery warning** at the dragon: funny or mean?
8. **Speech.** Eight clips in the same voice as Demo 2's JR, or a new
   one?

## What happens after you approve this

Checkpoint 2: the tools and the JavaScript interpreter, with two screens
playable in a browser: the Guest Room and Castle Steve, with JR
walking, the date prompt, the charge, and puzzle 1. You judge the look,
the walking, the parser and the text boxes on your phone or laptop.
