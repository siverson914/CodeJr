// Names for AGI's reserved variables and flags, and the game's own.

#define ego          o0
#define curRoom      v0
#define prevRoom     v1
#define egoEdge      v2
#define score        v3
#define egoDir       v6
#define maxScore     v7
#define unknownWord  v9
#define cycleDelay   v10
#define keyPressed   v19

#define onWater      f0
#define haveInput    f2
#define onTrigger    f3
#define handled      f4
#define newRoom      f5
#define soundOn      f9

// ---- engine flags (JRQ): set by talk() ----
#define demoNone     f27   // demo.play() found nobody to play it
#define talkEnded    f28
#define talkNoLink   f29

// ---- global game flags (30 to 79). See GAMEPLAN.md ----
#define gameInit     f30
#define introDone    f31
#define dateDone     f32
#define steveArrived f33
#define codeRead     f34
#define gateOpen     f35
#define gateMoved    f36
#define primeMet     f37
#define tookBook     f39
#define tookBucket   f40
#define tookByte     f41
#define edgeTold     f42
#define tuneDone     f43
#define dead         f44   // a room sets this (after set.string(s5, line)) to kill JR
#define outdoor      f45   // a room sets this in its init if Zuckerberg may wander in
#define knowsSI      f46   // the player has said "SI" at the Palace
#define ledgerRead   f47   // golf course water ledger (Palace)
#define meterRead    f48   // data center water meter
#define keeperTold   f49   // the Moat Keeper has heard both numbers
#define programFixed f50   // JR rewrote the wasteful program; the well is up
#define bucketFull   f51   // the bucket holds water
#define sigChamath   f52
#define sigJason     f53
#define sigFriedberg f54
#define sigSacks     f55
#define seedPlanted  f56
#define stalkGrown   f57
#define tokenInRing  f58
#define doorOpen     f59   // the Cathedral's Confident Door
#define agentsHome   f60   // the escaped agents are back in the Cathedral
#define agentsFollow f61   // they are following JR right now
#define lockDone     f62   // the wizards handed over the Lock half
#define dragonPassed f63
#define mirrorAwake  f64
#define gameWon      f65
#define zuckMet      f66
#define zuckHere     f67   // Zuckerberg is on this screen right now
#define debugOn      f68
#define signRead     f69   // the Token Meadow signpost
#define gotChest     f70
#define shieldMade   f71
#define wizardsTold  f72   // the wizards have told JR about the escaped agents
#define zuckTook     f73   // the player said yes to Zuckerberg once

// ---- room-local scratch flags: cleared on every room change ----
#define tmpA         f248
#define tmpB         f249
#define tmpC         f250
#define tmpD         f251

// ---- global game variables (30 to 79) ----
#define state        v30   // room-local by convention: set it in your init
#define work         v31   // scratch
#define dateResult   v32
#define dateMonth    v33
#define dateDay      v34
#define dateYear     v35
#define tries        v36
#define tokens       v40   // loose tokens JR carries (ignored once he has the Chest)
#define deaths       v41
#define deathRoom    v42
#define arriveFrom   v43   // how JR left the last room: 1 north 2 east 3 south 4 west, 5 door, 0 start
#define zuckOffer    v44   // how many times Zuckerberg has raised his offer
#define aiHelps      v45
#define unchanged    v46   // "parts that didn't change" found, of 9
#define hintLevel    v47
#define carried      v48
#define agiYear      v49   // how many times Musk has been asked when AGI arrives
#define entryEdge    v50   // arriveFrom as it was on entry, for Retry
#define work2        v51
#define work3        v52

// ---- room-local scratch variables: cleared on every room change ----
#define tA           v220
#define tB           v221
#define tC           v222
#define tD           v223
#define timer        v224

// ---- objects on screen. o1 to o11 belong to the room ----
#define steve        o1
#define gate         o2
#define squire       o3
#define prime        o4
#define bookObj      o5
#define bucketObj    o6
#define byteObj      o7
#define zuck         o12
#define swarm        o13

// ---- used only by the shared logics 80, 83 and 84 ----
#define zuckAsk      f74   // an offer is on the table: YES and NO mean something
#define swArrived    f75
#define memTold      f76   // the "context window" joke has been explained once
#define swX          v53
#define swY          v54
#define swDist       v55
#define zuckRoll     v56
#define steveTalk    v57   // which of King Steve's guest room lines comes next
