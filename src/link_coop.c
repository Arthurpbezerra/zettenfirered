#include "global.h"
#include "gflib.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "link.h"
#include "link_coop.h"
#include "link_proto.h"
#include "link_session.h"
#include "overworld.h"
#include "pokemon.h"
#include "constants/maps.h"

#define PRESENCE_INTERVAL 24
#define PRESENCE_TIMEOUT 90

struct LinkPresence
{
    u8 mapGroup;
    u8 mapNum;
    u8 direction;
    u8 gender;
    s16 x;
    s16 y;
    u32 rng; // reserved; RNG re-sync is Fase F
};

STATIC_ASSERT(sizeof(struct LinkPresence) <= LINK_PROTO_MAX_PAYLOAD, LinkPresenceFits);

struct LinkCoopCtx
{
    struct LinkPresence peer;
    u8 peerPlayerId;
    bool8 havePeer;
    u8 lastSeen;
    u8 sendTimer;
};

static struct LinkCoopCtx sCoop;

static bool8 MapIsWhitelisted(u8 mapGroup, u8 mapNum)
{
    u16 map = mapNum | (mapGroup << 8);

    return map == MAP_PALLET_TOWN || map == MAP_ROUTE1;
}

static bool8 PresenceFieldsSane(const struct LinkPresence *msg)
{
    if (msg->direction < DIR_SOUTH || msg->direction > DIR_EAST)
        return FALSE;
    if (msg->gender > FEMALE)
        return FALSE;
    return TRUE;
}

static bool8 PresenceCoordsOnLocalMap(const struct LinkPresence *msg)
{
    s32 width;
    s32 height;

    if (gMapHeader.mapLayout == NULL)
        return FALSE;
    width = gMapHeader.mapLayout->width;
    height = gMapHeader.mapLayout->height;
    if (msg->x < MAP_OFFSET || msg->y < MAP_OFFSET)
        return FALSE;
    if (msg->x >= MAP_OFFSET + width || msg->y >= MAP_OFFSET + height)
        return FALSE;
    return TRUE;
}

static void OnPresence(u8 playerId, const u8 *payload, u8 len)
{
    const struct LinkPresence *msg;

    if (len < sizeof(*msg))
        return;
    msg = (const struct LinkPresence *)payload;
    if (playerId >= MAX_LINK_PLAYERS)
        return;
    if (!PresenceFieldsSane(msg))
        return;
    sCoop.peer = *msg;
    sCoop.peerPlayerId = playerId;
    sCoop.havePeer = TRUE;
    sCoop.lastSeen = 0;
}

void LinkCoop_Reset(void)
{
    memset(&sCoop, 0, sizeof(sCoop));
}

void LinkCoop_Update(void)
{
    struct LinkPresence msg;
    s16 x, y;

    LinkProto_SetHandler(LINK_CHAN_PRESENCE, OnPresence);

    if (!LinkSession_IsEstablished())
    {
        LinkCoop_Reset();
        return;
    }

    if (sCoop.havePeer)
    {
        if (sCoop.lastSeen < 255)
            sCoop.lastSeen++;
        if (sCoop.lastSeen >= PRESENCE_TIMEOUT)
            sCoop.havePeer = FALSE;
    }

    if (++sCoop.sendTimer < PRESENCE_INTERVAL)
        return;

    // Presence yields the single SendBlock to control/app (trade request,
    // accept, ARRIVED). Keep sendTimer due so the next idle frame retries.
    if (LinkProto_HasPendingSend() || !IsLinkTaskFinished())
        return;

    sCoop.sendTimer = 0;

    PlayerGetDestCoords(&x, &y);
    memset(&msg, 0, sizeof(msg));
    msg.mapGroup = gSaveBlock1Ptr->location.mapGroup;
    msg.mapNum = gSaveBlock1Ptr->location.mapNum;
    msg.direction = GetPlayerFacingDirection();
    msg.gender = gSaveBlock2Ptr->playerGender;
    msg.x = x;
    msg.y = y;
    LinkProto_Send(LINK_CHAN_PRESENCE, &msg, sizeof(msg));
}

bool8 LinkCoop_IsActive(void)
{
    if (!LinkSession_IsEstablished() || !sCoop.havePeer)
        return FALSE;
    if (CalculatePlayerPartyCount() < 1)
        return FALSE;
    if (!MapIsWhitelisted(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum))
        return FALSE;
    if (sCoop.peer.mapGroup != gSaveBlock1Ptr->location.mapGroup
     || sCoop.peer.mapNum != gSaveBlock1Ptr->location.mapNum)
        return FALSE;
    return TRUE;
}

bool8 LinkCoop_ShouldSuppressFieldEvents(void)
{
    return LinkCoop_IsActive();
}

bool8 LinkCoop_GetPeerPose(u8 *playerId, s16 *x, s16 *y, u8 *direction, u8 *gender)
{
    if (!LinkCoop_IsActive())
        return FALSE;
    if (sCoop.peerPlayerId >= MAX_LINK_PLAYERS)
        return FALSE;
    if (!PresenceCoordsOnLocalMap(&sCoop.peer))
        return FALSE;
    *playerId = sCoop.peerPlayerId;
    *x = sCoop.peer.x;
    *y = sCoop.peer.y;
    *direction = sCoop.peer.direction;
    *gender = sCoop.peer.gender;
    return TRUE;
}
