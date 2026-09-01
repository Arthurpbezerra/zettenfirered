#include "global.h"
#include "gflib.h"
#include "event_scripts.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "link.h"
#include "link_coop.h"
#include "link_proto.h"
#include "link_session.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"

#define PRESENCE_INTERVAL 24
#define PRESENCE_TIMEOUT 90
#define FROZEN_HOLD_FRAMES 600

struct LinkPresence
{
    u8 mapGroup;
    u8 mapNum;
    u8 direction;
    u8 gender;
    u8 busy;
    s16 x;
    s16 y;
};

STATIC_ASSERT(sizeof(struct LinkPresence) <= LINK_PROTO_MAX_PAYLOAD, LinkPresenceFits);

struct LinkCoopCtx
{
    struct LinkPresence peer;
    u8 peerPlayerId;
    bool8 havePeer;
    u8 lastSeen;
    u8 sendTimer;
    bool8 wasTogether;
    bool8 leftMapMsgPending;
    bool8 sentBusy;
    bool8 frozen;
    u16 frozenTimer;
    u8 frozenPlayerId;
    u8 frozenDir;
    u8 frozenGender;
    s16 frozenX;
    s16 frozenY;
};

static struct LinkCoopCtx sCoop;

static bool8 PresenceFieldsSane(const struct LinkPresence *msg)
{
    if (msg->direction < DIR_SOUTH || msg->direction > DIR_EAST)
        return FALSE;
    if (msg->gender > FEMALE)
        return FALSE;
    if (msg->busy > 1)
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

static bool8 FrozenPoseOnLocalMap(void)
{
    struct LinkPresence pose;

    memset(&pose, 0, sizeof(pose));
    pose.x = sCoop.frozenX;
    pose.y = sCoop.frozenY;
    return PresenceCoordsOnLocalMap(&pose);
}

static bool8 LocalIsAwayFromSharedWorld(void)
{
    if (!Overworld_IsFieldCB2Active())
        return TRUE;
    if (ArePlayerFieldControlsLocked())
        return TRUE;
    if (ScriptContext_IsEnabled())
        return TRUE;
    return FALSE;
}

static bool8 CanStartCoopFieldAction(void)
{
    if (LocalIsAwayFromSharedWorld())
        return FALSE;
    if (gPaletteFade.active)
        return FALSE;
    return TRUE;
}

static void BeginFrozenHold(void)
{
    if (sCoop.frozen)
        return;
    if (!PresenceCoordsOnLocalMap(&sCoop.peer))
        return;
    if (sCoop.peer.mapGroup != gSaveBlock1Ptr->location.mapGroup
     || sCoop.peer.mapNum != gSaveBlock1Ptr->location.mapNum)
        return;
    sCoop.frozen = TRUE;
    sCoop.frozenTimer = 0;
    sCoop.frozenPlayerId = sCoop.peerPlayerId;
    sCoop.frozenDir = sCoop.peer.direction;
    sCoop.frozenGender = sCoop.peer.gender;
    sCoop.frozenX = sCoop.peer.x;
    sCoop.frozenY = sCoop.peer.y;
}

static void ClearFrozenHold(void)
{
    sCoop.frozen = FALSE;
    sCoop.frozenTimer = 0;
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
    if (msg->busy)
        BeginFrozenHold();
    else
        ClearFrozenHold();
}

static void TrackPeerMap(void)
{
    bool8 sameMap;

    sameMap = FALSE;
    if (sCoop.havePeer
     && !sCoop.peer.busy
     && sCoop.peer.mapGroup == gSaveBlock1Ptr->location.mapGroup
     && sCoop.peer.mapNum == gSaveBlock1Ptr->location.mapNum)
        sameMap = TRUE;

    if (sCoop.wasTogether && sCoop.havePeer && !sCoop.peer.busy && !sameMap)
    {
        sCoop.leftMapMsgPending = TRUE;
        ClearFrozenHold();
        sCoop.wasTogether = FALSE;
    }
    else if (sameMap)
        sCoop.wasTogether = TRUE;
    else if (!sCoop.havePeer)
        sCoop.wasTogether = FALSE;
}

static void TryShowLeftMapMsg(void)
{
    if (!sCoop.leftMapMsgPending)
        return;
    if (!CanStartCoopFieldAction())
        return;
    sCoop.leftMapMsgPending = FALSE;
    LockPlayerFieldControls();
    ScriptContext_SetupScript(EventScript_CoopPartnerLeftMap);
}

void LinkCoop_Reset(void)
{
    memset(&sCoop, 0, sizeof(sCoop));
}

void LinkCoop_Update(void)
{
    struct LinkPresence msg;
    s16 x, y;
    bool8 busy;

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
        {
            BeginFrozenHold();
            sCoop.havePeer = FALSE;
        }
    }

    if (sCoop.frozen)
    {
        if (sCoop.frozenTimer < 0xFFFF)
            sCoop.frozenTimer++;
        if (sCoop.frozenTimer >= FROZEN_HOLD_FRAMES)
            ClearFrozenHold();
    }

    TrackPeerMap();
    TryShowLeftMapMsg();

    busy = LocalIsAwayFromSharedWorld();
    if (busy != sCoop.sentBusy)
        sCoop.sendTimer = PRESENCE_INTERVAL;

    if (++sCoop.sendTimer < PRESENCE_INTERVAL)
        return;

    if (LinkProto_HasPendingSend() || !IsLinkTaskFinished())
        return;

    sCoop.sendTimer = 0;
    sCoop.sentBusy = busy;

    PlayerGetDestCoords(&x, &y);
    memset(&msg, 0, sizeof(msg));
    msg.mapGroup = gSaveBlock1Ptr->location.mapGroup;
    msg.mapNum = gSaveBlock1Ptr->location.mapNum;
    msg.direction = GetPlayerFacingDirection();
    msg.gender = gSaveBlock2Ptr->playerGender;
    msg.busy = busy;
    msg.x = x;
    msg.y = y;
    LinkProto_Send(LINK_CHAN_PRESENCE, &msg, sizeof(msg));
}

bool8 LinkCoop_IsActive(void)
{
    if (!LinkSession_IsEstablished() || !sCoop.havePeer)
        return FALSE;
    if (sCoop.peer.busy)
        return FALSE;
    if (LocalIsAwayFromSharedWorld())
        return FALSE;
    if (CalculatePlayerPartyCount() < 1)
        return FALSE;
    if (sCoop.peer.mapGroup != gSaveBlock1Ptr->location.mapGroup
     || sCoop.peer.mapNum != gSaveBlock1Ptr->location.mapNum)
        return FALSE;
    return TRUE;
}

bool8 LinkCoop_PeerPoseIsFrozen(void)
{
    return sCoop.frozen;
}

bool8 LinkCoop_GetPeerPose(u8 *playerId, s16 *x, s16 *y, u8 *direction, u8 *gender)
{
    if (LocalIsAwayFromSharedWorld())
        return FALSE;
    if (CalculatePlayerPartyCount() < 1)
        return FALSE;

    if (LinkCoop_IsActive())
    {
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

    if (!sCoop.frozen)
        return FALSE;
    if (sCoop.frozenPlayerId >= MAX_LINK_PLAYERS)
        return FALSE;
    if (!FrozenPoseOnLocalMap())
        return FALSE;
    *playerId = sCoop.frozenPlayerId;
    *x = sCoop.frozenX;
    *y = sCoop.frozenY;
    *direction = sCoop.frozenDir;
    *gender = sCoop.frozenGender;
    return TRUE;
}
