#ifndef GUARD_LINK_COOP_H
#define GUARD_LINK_COOP_H

#include "global.h"

// Overworld co-op v1: presence on a whitelist of maps. Wild encounters and
// NPC scripts stay off while both players share a listed map. The remote
// avatar is drawn by overworld.c from this pose; no extra EWRAM buffers.

void LinkCoop_Reset(void);
void LinkCoop_Update(void);
bool8 LinkCoop_IsActive(void);
bool8 LinkCoop_ShouldSuppressFieldEvents(void);
bool8 LinkCoop_GetPeerPose(u8 *playerId, s16 *x, s16 *y, u8 *direction, u8 *gender);

#endif // GUARD_LINK_COOP_H
