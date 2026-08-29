#include "global.h"
#include "gflib.h"
#include "event_data.h"
#include "link.h"
#include "link_diag.h"
#include "link_proto.h"
#include "link_coop.h"
#include "link_session.h"
#include "overworld.h"
#include "safari_zone.h"
#include "script.h"
#include "task.h"
#include "constants/vars.h"

// Sub-steps of LINK_SESSION_DISCOVERING. Kept private: callers only ever see
// the coarse state, so the handshake can be reworked without touching them.
enum
{
    DISCOVER_BOOT,
    DISCOVER_WAIT_PARTNER,
    DISCOVER_WAIT_EXCHANGE,
};

struct LinkSessionTimeouts
{
    u16 boot;
    u16 partner;
    u16 exchange;
    u16 dropGrace;
    u16 rendezvous;
};

static const struct LinkSessionTimeouts sTimeouts[] =
{
    [LINK_PROFILE_LOCAL]  = { .boot = 10, .partner =  360, .exchange =  360, .dropGrace =  45, .rendezvous =  480 },
    // Netplay adds a fixed input delay on top of every exchange, so the same
    // budgets would report a healthy connection as a timeout.
    [LINK_PROFILE_REMOTE] = { .boot = 20, .partner = 1200, .exchange = 1200, .dropGrace = 180, .rendezvous = 1200 },
};

struct LinkSessionCtx
{
    u8 state;
    u8 phase;
    u8 profile;
    bool8 enabled;
    bool8 advancedLinkState;
    bool8 handoffRequested;
    bool8 handoffReady;
    bool8 peerHandoffAck;
    bool8 ctrlSent;
    bool8 remoteHandoffSeen;
    bool8 peerArrived;
    bool8 arrivedSent;
    u8 arriveRetry;
    u8 peerCount;
    u8 peerIds[MAX_LINK_PLAYERS];
    u16 pendingLinkType;
    u16 remoteHandoffType;
    u16 timer;
};

static struct LinkSessionCtx sSession;

static const struct LinkSessionTimeouts *Timeouts(void)
{
    return &sTimeouts[sSession.profile];
}

enum
{
    CTRL_HANDOFF_REQ = 1,
    CTRL_HANDOFF_CANCEL = 2,
    CTRL_ARRIVED = 3,
};

struct LinkCtrlMsg
{
    u8 cmd;
    u8 pad;
    u16 linkType;
};

STATIC_ASSERT(sizeof(struct LinkCtrlMsg) <= LINK_PROTO_MAX_PAYLOAD, LinkCtrlMsgFits);

static void OnControlPacket(u8 playerId, const u8 *payload, u8 len)
{
    const struct LinkCtrlMsg *msg;

    (void)playerId;
    if (len < sizeof(*msg))
        return;
    msg = (const struct LinkCtrlMsg *)payload;

    if (msg->cmd == CTRL_HANDOFF_CANCEL)
    {
        LinkSession_CancelHandoff();
        return;
    }
    if (msg->cmd == CTRL_ARRIVED)
    {
        if (sSession.state == LINK_SESSION_HANDOFF)
            sSession.peerArrived = TRUE;
        return;
    }
    if (msg->cmd != CTRL_HANDOFF_REQ)
        return;
    if (sSession.state != LINK_SESSION_ESTABLISHED && sSession.state != LINK_SESSION_BARRIER)
        return;
    sSession.remoteHandoffSeen = TRUE;
    sSession.remoteHandoffType = msg->linkType;
    if (sSession.handoffRequested && msg->linkType == sSession.pendingLinkType)
        sSession.peerHandoffAck = TRUE;
}

void LinkSession_Init(void)
{
    memset(&sSession, 0, sizeof(sSession));
    sSession.state = LINK_SESSION_IDLE;
    sSession.profile = LINK_PROFILE_LOCAL;
    LinkProto_SetHandler(LINK_CHAN_CONTROL, OnControlPacket);
}

void LinkSession_SetProfile(u8 profile)
{
    if (profile < NELEMS(sTimeouts))
        sSession.profile = profile;
}

bool8 LinkSession_IsEnabled(void)
{
    return sSession.enabled;
}

u8 LinkSession_GetState(void)
{
    return sSession.state;
}

bool8 LinkSession_IsEstablished(void)
{
    return sSession.state == LINK_SESSION_ESTABLISHED && sSession.peerCount != 0;
}

u8 LinkSession_GetPeerCount(void)
{
    return sSession.peerCount;
}

const struct LinkPlayer *LinkSession_GetPeer(u8 index)
{
    if (index >= sSession.peerCount)
        return NULL;
    return &gLinkPlayers[sSession.peerIds[index]];
}

static void ClearPeers(void)
{
    sSession.peerCount = 0;
}

static void SyncPeers(void)
{
    u8 selfId = GetMultiplayerId();
    u8 count = GetLinkPlayerCount();
    u8 i;

    ClearPeers();
    if (count < 2 || count > MAX_LINK_PLAYERS)
        return;

    for (i = 0; i < count; i++)
    {
        if (i == selfId)
            continue;
        sSession.peerIds[sSession.peerCount++] = i;
    }
}

// May the session take the serial right now? Opening from a menu or a locked
// field deadlocks the other instance, which is why this is stricter than the
// test used to keep an already-open session alive.
static bool8 CanOpenLink(void)
{
    if (gPaletteFade.active)
        return FALSE;
    if (!Overworld_IsFieldCB2Active())
        return FALSE;
    if (ArePlayerFieldControlsLocked())
        return FALSE;
    return TRUE;
}

static bool8 ShouldKeepRunning(void)
{
    if (!sSession.enabled)
        return FALSE;
    if (gMain.inBattle)
        return FALSE;
    if (InUnionRoom() == TRUE)
        return FALSE;
    if (GetSafariZoneFlag() == TRUE)
        return FALSE;
    // VAR_CABLE_CLUB_STATE is save data. A leftover non-zero value must not
    // permanently disable discovery. Handoff is RAM-only (LINK_SESSION_HANDOFF)
    // and Phone_TryResumeLink already drops enabled while the club is busy.
    return TRUE;
}

static void CloseSerial(void)
{
    SetSuppressLinkErrorMessage(TRUE);
    CloseLink();
}

// Drop back to IDLE after a failed handshake. IDLE reopens on the next frame
// that allows it, so this doubles as the retry path.
static void RestartDiscovery(void)
{
    CloseSerial();
    ClearPeers();
    LinkCoop_Reset();
    sSession.state = LINK_SESSION_IDLE;
    sSession.phase = DISCOVER_BOOT;
    sSession.timer = 0;
    sSession.advancedLinkState = FALSE;
}

static void EnterDraining(void)
{
    sSession.state = LINK_SESSION_DRAINING;
    sSession.timer = 0;
}

void LinkSession_SetEnabled(bool8 enabled)
{
    if (sSession.enabled == enabled)
        return;

    sSession.enabled = enabled;
    if (!enabled)
    {
        sSession.handoffRequested = FALSE;
        sSession.handoffReady = FALSE;
        sSession.peerHandoffAck = FALSE;
        sSession.ctrlSent = FALSE;
        sSession.remoteHandoffSeen = FALSE;
        sSession.peerArrived = FALSE;
        sSession.arrivedSent = FALSE;
        sSession.arriveRetry = 0;
        sSession.pendingLinkType = 0;
        if (sSession.state != LINK_SESSION_IDLE && sSession.state != LINK_SESSION_HANDOFF)
            EnterDraining();
    }
}

void LinkSession_RequestHandoff(u16 linkType)
{
    if (sSession.state != LINK_SESSION_ESTABLISHED)
        return;
    sSession.pendingLinkType = linkType;
    sSession.handoffRequested = TRUE;
    sSession.handoffReady = FALSE;
    sSession.ctrlSent = FALSE;
    if (sSession.remoteHandoffSeen && sSession.remoteHandoffType == linkType)
        sSession.peerHandoffAck = TRUE;
    else
        sSession.peerHandoffAck = FALSE;
}

bool8 LinkSession_IsHandoffPending(void)
{
    return sSession.handoffRequested;
}

bool8 LinkSession_IsHandoffReady(void)
{
    return sSession.handoffReady;
}

void LinkSession_BeginHandoff(void)
{
    if (!sSession.handoffReady)
        return;
    sSession.state = LINK_SESSION_HANDOFF;
    sSession.timer = 0;
    sSession.peerArrived = FALSE;
    sSession.arrivedSent = FALSE;
    sSession.arriveRetry = 0;
}

void LinkSession_NotifyArrived(void)
{
    struct LinkCtrlMsg msg;

    if (sSession.state != LINK_SESSION_HANDOFF)
        return;
    if (sSession.peerArrived && sSession.arrivedSent)
        return;
    if (!IsLinkTaskFinished())
        return;
    if (sSession.arriveRetry != 0)
    {
        sSession.arriveRetry--;
        return;
    }

    memset(&msg, 0, sizeof(msg));
    msg.cmd = CTRL_ARRIVED;
    msg.linkType = sSession.pendingLinkType;
    if (LinkProto_Send(LINK_CHAN_CONTROL, &msg, sizeof(msg)))
    {
        sSession.arrivedSent = TRUE;
        sSession.arriveRetry = 30;
    }
}

bool8 LinkSession_PeerHasArrived(void)
{
    return sSession.peerArrived;
}

bool8 LinkSession_RendezvousComplete(void)
{
    return sSession.peerArrived && sSession.arrivedSent;
}

u16 LinkSession_GetRendezvousBudget(void)
{
    return Timeouts()->rendezvous;
}

void LinkSession_CancelHandoff(void)
{
    sSession.handoffRequested = FALSE;
    sSession.handoffReady = FALSE;
    sSession.peerHandoffAck = FALSE;
    sSession.ctrlSent = FALSE;
    sSession.remoteHandoffSeen = FALSE;
    sSession.peerArrived = FALSE;
    sSession.arrivedSent = FALSE;
    sSession.arriveRetry = 0;
    sSession.pendingLinkType = 0;
    if (sSession.state == LINK_SESSION_BARRIER)
        sSession.state = LINK_SESSION_ESTABLISHED;
}

void LinkSession_EndHandoff(void)
{
    sSession.handoffRequested = FALSE;
    sSession.handoffReady = FALSE;
    sSession.peerHandoffAck = FALSE;
    sSession.ctrlSent = FALSE;
    sSession.remoteHandoffSeen = FALSE;
    sSession.peerArrived = FALSE;
    sSession.arrivedSent = FALSE;
    sSession.arriveRetry = 0;
    sSession.pendingLinkType = 0;

    if (sSession.state != LINK_SESSION_HANDOFF)
        return;

    // The activity engine may or may not have left the serial usable. If the
    // peers are still there the session resumes in place; otherwise it
    // rediscovers, which is the same path a transient error takes.
    if (gReceivedRemoteLinkPlayers && GetLinkPlayerCount_2() >= 2)
    {
        gLinkType = LINKTYPE_PHONE;
        SyncPeers();
        LinkProto_Reset();
        LinkCoop_Reset();
        sSession.state = LINK_SESSION_ESTABLISHED;
        sSession.timer = 0;
    }
    else
    {
        RestartDiscovery();
    }
}

static void UpdateIdle(void)
{
    if (!ShouldKeepRunning())
        return;
    if (!CanOpenLink())
        return;

    // Another subsystem already owns an established link of a different type.
    // Taking the serial from it would corrupt both sides.
    if (gReceivedRemoteLinkPlayers && gLinkType != LINKTYPE_PHONE)
        return;

    if (gReceivedRemoteLinkPlayers && gLinkType == LINKTYPE_PHONE)
    {
        SyncPeers();
        LinkProto_Reset();
        LinkCoop_Reset();
        sSession.state = LINK_SESSION_ESTABLISHED;
        sSession.timer = 0;
        return;
    }

    gWirelessCommType = 0;
    gLinkType = LINKTYPE_PHONE;
    OpenLinkTimed();
    SetSuppressLinkErrorMessage(TRUE);
    ResetLinkPlayers();
    ResetLinkPlayerCount();
    LinkDiag_Count(LINK_DIAG_SESSION_OPENED);

    sSession.state = LINK_SESSION_DISCOVERING;
    sSession.phase = DISCOVER_BOOT;
    sSession.timer = 0;
    sSession.advancedLinkState = FALSE;
}

static void UpdateDiscovering(void)
{
    const struct LinkSessionTimeouts *t = Timeouts();
    u8 status;

    if (!ShouldKeepRunning())
    {
        EnterDraining();
        return;
    }

    switch (sSession.phase)
    {
    case DISCOVER_BOOT:
        if (++sSession.timer > t->boot)
        {
            sSession.timer = 0;
            sSession.phase = DISCOVER_WAIT_PARTNER;
        }
        break;

    case DISCOVER_WAIT_PARTNER:
        if (HasLinkErrorOccurred() == TRUE)
        {
            RestartDiscovery();
            break;
        }
        if (GetLinkPlayerCount_2() >= 2)
        {
            if (IsLinkMaster() == TRUE && !sSession.advancedLinkState)
            {
                CheckShouldAdvanceLinkState();
                sSession.advancedLinkState = TRUE;
            }
            sSession.timer = 0;
            sSession.phase = DISCOVER_WAIT_EXCHANGE;
        }
        else if (++sSession.timer > t->partner)
        {
            LinkDiag_Count(LINK_DIAG_TIMEOUT);
            RestartDiscovery();
        }
        break;

    case DISCOVER_WAIT_EXCHANGE:
        status = GetLinkPlayerDataExchangeStatusTimed(2, MAX_LINK_PLAYERS);
        if (status == EXCHANGE_COMPLETE)
        {
            SyncPeers();
            LinkProto_Reset();
            LinkCoop_Reset();
            LinkDiag_Count(LINK_DIAG_SESSION_ESTABLISHED);
            sSession.state = LINK_SESSION_ESTABLISHED;
            sSession.timer = 0;
        }
        else if (status == EXCHANGE_TIMED_OUT
              || status == EXCHANGE_WRONG_NUM_PLAYERS
              || status == EXCHANGE_DIFF_SELECTIONS
              || HasLinkErrorOccurred() == TRUE)
        {
            RestartDiscovery();
        }
        else if (++sSession.timer > t->exchange)
        {
            LinkDiag_Count(LINK_DIAG_TIMEOUT);
            RestartDiscovery();
        }
        break;
    }
}

static void UpdateEstablished(void)
{
    const struct LinkSessionTimeouts *t = Timeouts();

    LinkDiag_SampleLinkStatus();

    if (!ShouldKeepRunning())
    {
        EnterDraining();
        return;
    }

    if (HasLinkErrorOccurred() == TRUE
     || GetLinkPlayerCount_2() < 2
     || !gReceivedRemoteLinkPlayers)
    {
        // A brief dropout is normal on netplay; only give up after the grace
        // window, otherwise every hiccup costs a full rediscovery.
        if (++sSession.timer > t->dropGrace)
        {
            LinkDiag_Count(LINK_DIAG_SESSION_DROPPED);
            RestartDiscovery();
        }
        return;
    }

    sSession.timer = 0;
    SyncPeers();
    LinkProto_Poll();

    if (sSession.handoffRequested)
    {
        sSession.state = LINK_SESSION_BARRIER;
        sSession.timer = 0;
    }
}

static void UpdateBarrier(void)
{
    const struct LinkSessionTimeouts *t = Timeouts();
    struct LinkCtrlMsg msg;

    LinkDiag_SampleLinkStatus();
    LinkProto_Poll();

    if (!sSession.handoffRequested)
    {
        sSession.state = LINK_SESSION_ESTABLISHED;
        sSession.ctrlSent = FALSE;
        sSession.peerHandoffAck = FALSE;
        return;
    }

    if (HasLinkErrorOccurred() == TRUE || !gReceivedRemoteLinkPlayers)
    {
        RestartDiscovery();
        return;
    }

    if (!sSession.ctrlSent && IsLinkTaskFinished())
    {
        memset(&msg, 0, sizeof(msg));
        msg.cmd = CTRL_HANDOFF_REQ;
        msg.linkType = sSession.pendingLinkType;
        if (LinkProto_Send(LINK_CHAN_CONTROL, &msg, sizeof(msg)))
            sSession.ctrlSent = TRUE;
    }

    if (sSession.peerHandoffAck && IsLinkTaskFinished())
    {
        sSession.handoffReady = TRUE;
        return;
    }

    if (++sSession.timer > t->exchange)
    {
        LinkDiag_Count(LINK_DIAG_TIMEOUT);
        if (IsLinkTaskFinished())
        {
            memset(&msg, 0, sizeof(msg));
            msg.cmd = CTRL_HANDOFF_CANCEL;
            LinkProto_Send(LINK_CHAN_CONTROL, &msg, sizeof(msg));
        }
        LinkSession_CancelHandoff();
    }
}

static void UpdateDraining(void)
{
    CloseSerial();
    ResetSerial();
    ClearPeers();
    LinkCoop_Reset();
    sSession.state = LINK_SESSION_IDLE;
    sSession.phase = DISCOVER_BOOT;
    sSession.timer = 0;
    sSession.advancedLinkState = FALSE;
}

void LinkSession_Update(void)
{
    LinkProto_SetHandler(LINK_CHAN_CONTROL, OnControlPacket);

    switch (sSession.state)
    {
    case LINK_SESSION_IDLE:
        UpdateIdle();
        break;
    case LINK_SESSION_DISCOVERING:
        UpdateDiscovering();
        break;
    case LINK_SESSION_ESTABLISHED:
        UpdateEstablished();
        break;
    case LINK_SESSION_BARRIER:
        UpdateBarrier();
        break;
    case LINK_SESSION_HANDOFF:
        // The vanilla battle/trade engine owns the serial here. Touching it
        // would desynchronise both sides.
        break;
    case LINK_SESSION_DRAINING:
        UpdateDraining();
        break;
    }
}
