// Room 13, the Cathedral: its own names. Flags 200 to 211, variables 160
// to 165, views 150 to 159.
#define sam        o1
#define dario      o2
#define samA       o3
#define samB       o4
#define darA       o5
#define darB       o6
#define door       o7
#define swarmObj   o8

#define doorAsked  f200   // the Door has put its question once
#define doorMoved  f201   // the Door finished swinging open
#define samOut     f202   // Sam reached his step
#define darioOut   f203   // Dario reached his step
#define swarmIn    f204   // the agents reached the door
#define samNear    f205   // the wizards have stepped up to take JR's hands
#define darioNear  f206
#define priceSeen  f207

#define wrongTries v160   // confident answers given to the Door
#define gone       v161   // apprentices hired away so far (zuckOffer / 2)
