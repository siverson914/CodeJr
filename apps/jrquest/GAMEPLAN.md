# Jr's Quest: the build plan for checkpoint 3

`DESIGN.md` says what the game is. `FORMATS.md` says how files are
written. This file says who owns which numbers and what every room must
do so that sixteen rooms built by different hands fit together.

Rooms 1 and 2 are built: read `game/pic/002_castle.pic`,
`game/logic/002_castle.lgc`, `game/viewgen/000_core.js` and
`test/checkpoint2.txt` before writing anything. They are the house style.

## The map

```
              col 1            col 2             col 3              col 4
  north   11 Launch Pad     12 Golden Palace  13 The Cathedral   14 GPU Field
  middle   6 Data Center     7 Sand Hill Br.   8 The Bazaar       9 Tavern (outside)
  south    2 Castle Steve    3 Token Meadow    4 Godfathers' Inn  5 Cold Reactor
```

It wraps both ways. Interiors: 1 Guest Room (door in room 2), 10 Tavern
(door in room 9), 15 The Cloud (up the beanstalk in room 14), 16 Dragon's
Cave (cave mouth in room 5).

| Room | North | East | South | West |
|---|---|---|---|---|
| 2 | closed (the house) | 3 | 11 | 5 |
| 3 | 7 | 4 | 12 | 2 |
| 4 | 8 | 5 | 13 | 3 |
| 5 | 9 | 2 | 14 | 4 |
| 6 | 11 | 7 | closed (the moat) | 9 |
| 7 | 12 | 8 | 3 | 6 |
| 8 | 13 | 9 | 4 | 7 |
| 9 | 14 | 6 | 5 | 8 |
| 11 | 2 | 12 | 6 | 14 |
| 12 | 3 | 13 | 7 | 11 |
| 13 | 4 | 14 | 8 | 12 |
| 14 | 5 | 11 | 9 | 13 |

Every open edge must really be reachable on foot:

- **South edge:** a walkable band across most of the bottom of the
  picture. JR leaves when his baseline passes y 167.
- **West and east edges:** walkable at the edge somewhere between y 120
  and y 167.
- **North edge:** a walkable lane at least 16 pixels wide that runs up to
  the horizon (`set.horizon(h)`; JR leaves north when he reaches it).
  Put it beside the buildings, not through them.
- A closed edge gets a control line (priority 0) or a short message and a
  step back, never an invisible wall with no explanation.

Leaving: `arriveFrom = N; new.room(R);` with N = 1 north, 2 east, 3
south, 4 west, 5 through a door or other special way. Arriving: the
room's init reads `arriveFrom` and puts JR at its own fixed entry point
for the opposite side (left by going north = arrives at this room's
south entry, and so on). `arriveFrom` 0 (a test teleport) uses the
south entry. Entry points must be on walkable ground, clear of every
trigger line.

## Numbers each room owns

N is the room number, 3 to 16.

| Thing | Yours | Shared (don't define, do use) |
|---|---|---|
| Logic | N. Helper logics, if you want them: 100 + N | 0 main, 90 boot, 91 room init, 95 debug, 99 death, 80 Zuckerberg, 84 the agent swarm |
| Picture | N. Extra pictures: 100 + N | |
| Views | N*10 + 20 to N*10 + 29 | 0 JR, 1 Steve, 2 to 7 castle things, 8 Zuckerberg, 9 the agent swarm, 10 to 19 reserved |
| Sounds | N*10 + 20 to N*10 + 29 | 1 title, 2 to 19 reserved |
| Flags (kept all game) | 80 + (N-3)*12, twelve of them | 30 to 79 in `defines.h` |
| Flags (scratch, cleared on room change) | | `tmpA` to `tmpD` (248 to 251) |
| Variables (kept all game) | 100 + (N-3)*6, six of them | 30 to 79 in `defines.h` |
| Variables (scratch, cleared on room change) | | `state`, `work`, `tA` to `tD`, `timer` |
| Screen objects | o1 to o11 | o0 JR, o12 Zuckerberg, o13 the swarm |
| Words | `game/words/NN_name.txt`, groups N*100 to N*100+99 | everything in `game/words.txt` |
| Your defines | `game/logic/roomNN.h`, included after `defines.h` | `game/logic/defines.h` |
| Sprite drawings | `game/viewgen/NN_name.js` | `tools/viewkit.js` |
| Test script | `test/rooms/NN_name.txt` | |

Files you may create or edit: only the ones in the "Yours" column for
your rooms. `game/words.txt`, `game/objects.txt`, `game/logic/defines.h`,
logics 0, 1, 2, 80, 84, 90, 91, 95, 99, everything in `js/` and `tools/`
are not yours: if you need a shared word, an inventory item, a global
flag or an engine fix, say so in your report and work around it for now.
A word may be in only one group in the whole game, so search
`game/words.txt` and `game/words/` before adding one.

## What every room's logic does

```
#include "defines.h"
#include "room07.h"

if (isset(newRoom)) {
  load.pic(curRoom); draw.pic(curRoom); discard.pic(curRoom);
  load.view(...);
  set.horizon(60);
  call(91);                       // JR on the list, standing still, scratch cleared
  set(outdoor);                   // outdoor rooms only: Zuckerberg may wander in
  if (arriveFrom == 1) { position(ego, 75, 160); }   // came north, so he is at our south
  ...
  // your people and things: animate.obj, set.view, position, draw
  draw(ego);
  show.pic();
}
// edges
if (egoEdge == 2) { arriveFrom = 2; new.room(8); }
...
// said() handlers
return();
```

- Logic 0 runs first and last. After your room returns it handles the
  global commands (look me, inventory, help, join shield, save, restore)
  and prints "Bad command or file name." for anything nobody accepted.
  So handle what belongs to your room and leave the rest alone.
- **Every room answers at least:** `look` (the whole screen), `look` at
  each visible thing and person, `talk` to each person (different lines
  before and after their puzzle), the obvious wrong tries for its puzzle
  (with a line that points the right way), and `take` for anything that
  looks takeable. Twenty to forty handlers a room is normal.
- A person JR must be near: test `posn(ego, x1, y1, x2, y2)` and say
  "Walk closer." otherwise.
- **JR walks 2 pixels a step.** Any trigger line (priority 2) is two rows
  thick, with a solid row (priority 0) behind it. See the doors in rooms
  1 and 2.
- Inventory: `get("Seed")`, `has("Seed")`, `drop("wool")` (drop removes
  it from the game: room 0). Items are in `game/objects.txt`. Nothing
  JR needs may ever be lost: if he gives a thing away, the puzzle is
  solved by that.
- Tokens: `tokens` (v40) is how many loose tokens JR has. To charge n:
  if he has the Chest of Tokens it costs nothing and never runs out;
  otherwise test `tokens > n-1`, then `tokens -= n`.
- Score: add the points in the table below once, with `score += n`.
- **Killing JR:** `set.string(s5, "The death line."); set(dead);` Logic 0
  does the rest (the `Abort, Retry, Fail?` screen, and Retry puts him
  back at the entry he came in by). Never leave him in a state where
  Retry kills him again at once.
- "Parts that didn't change": where the table below gives your room one,
  the first time the player sees that joke do `unchanged++` (guard it
  with one of your flags).
- Messages are shown in a 30-column box. Keep one `print` under about
  200 characters and split long speeches into several. Dialogue is
  written `NAME: line`, as in room 2. Plain ASCII only. No em dashes.

## Writing rules for real people

Read "Who is in it, and what each joke rests on" and "The facts" in
`DESIGN.md`. Then:

- Every joke about a real person rests on a fact in that table. Don't add
  new claims about real people from memory.
- Never put a real quote in someone's mouth reworded, and never invent a
  quote that could pass for real. The scene is a fairy tale and the lines
  are plainly a fairy tale's.
- Everyone gets a fair point as well as a joke. The game likes these
  people more than it lets on.
- JR is 42, innocent, proud of small numbers, and honest. The world
  around him is fast, confident and a bit careless. That contrast is the
  joke every time.
- Puzzle answers are true facts the game has shown the player first.

## Drawing rules

- Use `node tools/pic.js game/pic/NNN_name.pic --png /path/x.png --pri
  /path/p.png` and LOOK at both PNGs (Read them) after every change.
  Write temporary files only under your own folder in the scratchpad
  directory you are given.
- A fill only replaces white. So: all outlines first, then all fills. A
  line drawn across a shape stays visible, so things "behind" must stop
  at the edge of things in front. A region you leave unfilled is white.
- Never use white (15) in a `pattern`.
- Look for leaks: a fill that floods the sky or the whole priority layer.
  Close every outline. `rect`, `poly`, `box`, `circle` help.
- Shades the first two rooms use, so the kingdom looks like one place:
  sky `color 9` on top and `color 11` / `pattern 1 9` near the horizon;
  sea `color 1` / `pattern 4 9`; grass `color 2` / `pattern 6 10`; sand
  path `color 14` / `pattern 1 6`; stone `color 8` / `pattern 1 7`;
  stucco `color 14` / `pattern 1 7`; terracotta `color 12` / `pattern 1
  6`; wood floor `color 4` / `pattern 12 6`.
- Use arcs for anything round (hills, domes, clouds, the sun) and at
  least two dithered fills a room: they are what this engine has that
  1984 Sierra did not.
- Every outdoor room has sky, a horizon between y 40 and y 70, and its
  landmark filling the middle. Computer parts hide in the fantasy (a
  bridge of flat keys, a gate that is a cartridge slot).
- Priority: the picture starts at priority 4 everywhere. Anything JR can
  walk behind gets the band of its base: `5 + (y - 48) * 10 / 120`
  rounded down (y 96 is band 9, y 120 band 11, y 144 band 13). Fill the
  thing's whole shape on the priority layer, and keep that outline
  closed.
- Control lines: priority 0 stops everyone, 2 is a trigger (sets flag 3
  while JR stands on it), 3 is water (flag 0). JR is 10 wide and his
  baseline is his bottom row.
- People are views, not picture. Use `kit.person({...})` from
  `tools/viewkit.js` for a 9 by 27 person and change colors, hat rows
  and single rows to make each one recognizable at a glance (hair,
  glasses, a jacket, a tie). Bigger things (a giant, a dragon, a rocket
  flame) are your own grids. Preview with `node tools/mkviews.js
  game/view --png /path/sheet.png`.

## Testing

- Build into your own folder so you never fight over `build/`:
  `node tools/pack.js game /path/to/your/build`.
- If the build fails on a file that isn't yours, another agent is
  mid-edit: wait a minute and try again. Keep your own files compiling
  at every save.
- `test/rooms/NN_name.txt` is a `tools/play.js` script that starts with
  `type debug` (skips the opening and puts JR in room 1), goes to your
  room with `type tp` then `type N`, sets what it needs with `type
  gimme` + item number or `type setflag` + flag number, and then solves
  your puzzles the way a player would, with `expect` lines after every
  step. Also include the wrong tries. Look at `--png-dir` screenshots:
  the picture with people standing in it is the real test of the art.
- Walk JR in your tests, don't only teleport: check he can reach every
  person, every exit and every trigger, and cannot walk through walls,
  water or furniture.

## The rooms

Points are from `DESIGN.md`'s puzzle chain. Jokes and lines are there
too ("The puzzle chain", "The Tavern", "Jokes from Demo 2", "Who is in
it", "The script"): use them, then write the other thirty handlers in
the same voice.

### 3 Token Meadow (6 points)
Tokens grow like flowers: pieces of words (`un`, `believ`, `able`, `ing`,
` the`). A signpost charges by the token to read. Pick tokens: +2
tokens a pick, up to 12 in hand before the Chest; they grow back. First
pick: 3 points, and the lesson (a token is a piece of a word; you pay
per token; answers cost more than questions). Paying the signpost 3
tokens and reading it: 3 points, `set(signRead)`; it gives the map of
the kingdom in words. DHH's cottage with tiled, never-overlapping
windows; DHH outside with a row of fencing apprentices and a race car.
The tiling exchange is a "part that didn't change" ("That part came
back!"). A cashmere goat: `pet goat` gives the item `wool` (once; he
needs it for Chamath).

### 4 Godfathers' Inn (0 points)
Outside view of an inn with tables in the yard, so no interior is
needed. Geoffrey Hinton and Yann LeCun at one table: ten percent and
zero. Gary Marcus at the next table ("it's not reliable", including the
soup): he grades everything and holds the `answer key`; he gives it once
`wizardsTold` is set (before that he says he's saving it for an
emergency). The almanac ("2027" with "2030" pasted over), the petition,
Yudkowsky and Soares's book on a shelf. Sir Tandy, a knight in JR's
exact colors who gets the credit: "part that didn't change" (the best of
me made it, under someone else's name). **Hints:** `hint` (or asking
either godfather for help) gives the next step from the global flags in
`defines.h`, plainer each time it is asked for the same step. Write the
hint ladder for the whole puzzle chain.

### 5 Cold Reactor (0 points)
A nuclear plant with no steam. A guard with a clipboard: "Restarting
soon." The "A nuclear plant! To do what?" / "A lot of it is email."
exchange. Behind the cooling tower, a cave mouth: a door trigger to room
16 (`arriveFrom = 5`). Coming back out of the cave arrives beside it.

### 6 Data Center (20 points)
A windowless castle with a moat along the south (the south edge is
closed; swimming is a death: "You are a 1984 computer. You were not
rated for water."). The Moat Keeper by the well. Steps: `read meter` (4,
`set(meterRead)`): 17 billion gallons for all of them in the country in
a year. Telling her both numbers needs `ledgerRead` too (the golf
ledger, 547 billion, from room 12): 6 points, `set(keeperTold)`; she
grants the national point and makes the local one (this town's well is
low; the power plant upstream drinks twelve times what the building
does). `look in window`: a hall of machines blinking one cursor.
`rewrite program` (needs `keeperTold`): 6 points, `set(programFixed)`,
"forty bytes", the fans slow, the well rises (show it). `fill bucket`
(needs `programFixed` and the bucket): 4 points, `set(bucketFull)`.
Never let the game say data centers use "no" or "little" water.

### 7 Sand Hill Bridge (0 points)
A bridge of flat square keys over a stream of money (green, with coins).
The Toll Troll takes 15% of loose tokens to cross west to east or back
(round up, at least 1 if JR has any; free with the Chest, and free with
none: "Fifteen percent of nothing. Go on."). "I asked for twenty." The
chiclet exchange is a "part that didn't change". A wanted poster.
Venture offices up the hill for scenery.

### 8 The Bazaar (12 points)
Stalls for DeepSeek, Qwen and Kimi; cheap, loud, open. The stallkeeper
sells the Glass half for 1 token, but only to someone who has published
their own source: `give book` (the Technical Reference; it stays with
the stall) gets `Glass half`, 12 points. The art seller and the
sixteen-color painting priced at sixteen million tokens: "part that
didn't change". The $589 billion day. Chamath's order of Kimi waiting
at a stall.

### 9 Tavern, outside (0 points) and 10 Tavern, inside (20 points)
Outside: the building, a sign, a bouncer and a $7,500 cover. Saying who
sent him (`say steve`, `talk bouncer`, anything honest) gets JR in: "A
listener? We have a listener!" The door is a trigger to room 10.
Inside: four chairs with the nicknames carved on them, the four hosts,
$1,200 tequila. Four jobs, 5 points each, any order, exactly as in
`DESIGN.md` "The Tavern": wool for Chamath (`sigChamath`), ask Jason
about Uber (`sigJason`; every other topic turns toward it), sit through
Science Corner for Friedberg (`sigFriedberg`; the other three leave the
screen and come back; five message boxes; he pays in `quinoa`), say
"SI" to Sacks (`sigSacks`; needs `knowsSI`, and saying "AI" gets
refused). With all four: the `Seed`. The door back out is at the bottom.

### 11 Launch Pad (0 points; half of the agents puzzle)
A rocket with a server rack on top, a row of gas turbines, Elon Musk, a
shop sign repainted three times (xAI, SpaceXAI, SpaceXSI). `talk musk
about agi`: "next year", and the year named goes up by one each time
(`agiYear`). The empty hourglass. Deaths: touching a turbine, and
standing on the pad when the rocket test-fires (give a clear warning
first: a countdown anyone can walk away from). The escaped agents (view
9, a little swarm) mill about by the pad once `wizardsTold` is set and
until `agentsHome`. Walk in holding the `answer key` and they notice:
`set(agentsFollow)`. From then on logic 84 moves them; your room only
shows them before that.

### 12 Golden Palace (10 points)
A gold palace with a golf course beside it. Donald Trump on the balcony.
A painter changing every "AI" on every sign to "SI". Guards. On this
screen people do not acknowledge the word AI: any sentence with "ai" in
it gets "(The guard does not acknowledge that word.)". The first
sentence with "si" in it: 5 points, `set(knowsSI)`, and they answer. The
"morally binding" scroll on the wall (taking it gives `Accord scroll`,
which is useless and says so). The groundskeeper's shed and its ledger:
`read ledger`, 5 points, `set(ledgerRead)`: the kingdom's golf courses
used about 547 billion gallons in a year.

### 13 The Cathedral (10 + 12 + 6 points)
The closed labs. The Confident Door asks a question nobody could answer
("How many leaves are on the beanstalk?"). Any confident answer keeps
it shut. `say i don't know` (group 217): 10 points, `set(doorOpen)`.
Then Sam Altman and Dario Amodei on the steps, who will not hold hands.
First talk: their test agents got out to find the answers to their own
exam; `set(wizardsTold)`. When JR arrives with `agentsFollow` set: the
agents go in, 12 points, `set(agentsHome)`, `reset(agentsFollow)`,
`drop("answer key")`. Then paying 50 tokens (needs the Chest) and the
hand-holding: `Lock half`, 6 points, `set(lockDone)`. Each wizard has a
newer apprentice behind him and a newer one behind that; one fewer each
time `zuckOffer` goes up by 2. The price list ("Twenty dollars a month.
Forever." / "I'm all paid for.").

### 14 GPU Field (10 points)
Rows of graphics cards growing like corn. "Built for games. Then
somebody tried thinking on them." / "I was built for games too!" is a
"part that didn't change". `plant seed` (4, `set(seedPlanted)`, drops
the Seed), `pour water` (needs `bucketFull`; 6, `set(stalkGrown)`,
`reset(bucketFull)`): the beanstalk grows as a chart going up and to the
right (animate it). `climb` goes to room 15 (`arriveFrom = 5`).

### 15 The Cloud (18 points)
JR's first words: "That's a mainframe." ("part that didn't change").
White ground with edges: walking off is a death. Jensen Huang, a giant
in a leather jacket, holding the Chest. Three merchants in a ring.
`give token to merchant` (costs 1 token; 8 points, `set(tokenInRing)`):
the token goes round the ring forever (animate it) and the giant can't
look away. `take chest` then: `Chest of Tokens`, 10 points,
`set(gotChest)`; before the ring it is refused. `climb down` returns to
room 14.

### 16 Dragon's Cave (8 + 8 points)
A dragon breathing fire across the only path (animate the fire). With
the `Shield`, JR walks through: 8 points, `set(dragonPassed)`. Without:
death ("There was only a twenty percent chance of that."). Give a
warning line as he gets close so it is fair. Beyond: the Mirror. Asleep
it says "You're absolutely right!" to everything. `give chest` (it
needs tokens to think; JR keeps the Chest, the Mirror only needs to be
shown it) and `tell mirror the date` (the date the player typed at the
start is in `dateMonth`, `dateDay`, `dateYear` + 1980: print it):
`set(mirrorAwake)`, 8 points, "...Hello?" / "Somebody has to tell me,
too." ("part that didn't change"). `take mirror`: the `Mirror` item.
The way out is the way in (to room 5, `arriveFrom = 5`).

### Parts that didn't change (9)
3 tiling, 4 Tandy, 7 chiclet keys, 8 sixteen colors, 14 built for
games, 15 the mainframe, 16 the date, plus two in the ending (room 1).
