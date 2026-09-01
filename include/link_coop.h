#ifndef GUARD_LINK_COOP_H
#define GUARD_LINK_COOP_H

#include "global.h"

// Overworld co-op: presence on any map. The remote avatar appears when both
// players share the same map, the local party has at least one Pokémon, and
// the local player is idle. If the peer enters a script/battle, their sprite
// stays frozen for a few seconds, then despawns until they are idle again.
// Wild encounters and cutscenes stay local to each save.

void LinkCoop_Reset(void);
void LinkCoop_Update(void);
bool8 LinkCoop_IsActive(void);
bool8 LinkCoop_PeerPoseIsFrozen(void);
bool8 LinkCoop_GetPeerPose(u8 *playerId, s16 *x, s16 *y, u8 *direction, u8 *gender);

#endif // GUARD_LINK_COOP_H
