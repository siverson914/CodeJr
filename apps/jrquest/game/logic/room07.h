// Room 7, Sand Hill Bridge: its own names. Flags 128 to 139, variables 124 to 129.
#define bTrollMet    f128  // the troll has introduced himself
#define bChiclet     f129  // the chiclet exchange: a part that didn't change
#define bChestWaved  f130  // the troll has waved the Chest through, once and for all
#define bEmptyWaved  f131  // the troll has waved an empty pocket through
#define bTalk        v124  // which of the troll's lines comes next

#define troll        o1
#define coin         o2
#define note         o3
#define coin2        o4
#define poster       o5

// scratch for this room
#define tollPaid     tmpA  // he has paid for the crossing he is making
#define coinDone     tmpB
#define noteDone     tmpC
#define coin2Done    tmpD
#define bank         tA    // the bank JR last stood on: 1 west, 2 east
#define paidFrom     tB    // the bank he was coming from when he paid
