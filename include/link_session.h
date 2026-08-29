#ifndef GUARD_LINK_SESSION_H
#define GUARD_LINK_SESSION_H

#include "global.h"
#include "link.h"

// Sole owner of the serial hardware for the custom multiplayer stack.
//
// Contract, enforced by review:
//   - Nothing outside this module calls OpenLink/OpenLinkTimed/CloseLink/
//     ResetSerial while a session is enabled.
//   - The session is NOT a task. Its state lives here, not in gTasks[].data,
//     so a ResetTasks() from a screen transition cannot destroy it and no
//     stash/restore dance is needed.
//   - LinkSession_Update() performs at most one state transition per call and
//     must be called once per frame from whatever loop currently owns the CPU
//     (the overworld CB1 and the Agenda's main loop both do).
//   - The link is never torn down to switch activities. Battle and trade are
//     reached through the barrier/handoff path with the serial still live.

enum LinkSessionState
{
    LINK_SESSION_IDLE,        // serial closed, nothing running
    LINK_SESSION_DISCOVERING, // serial open, waiting for a peer
    LINK_SESSION_ESTABLISHED, // peer present, application traffic allowed
    LINK_SESSION_BARRIER,     // both sides agreeing to switch activity
    LINK_SESSION_HANDOFF,     // vanilla battle/trade engine owns the link
    LINK_SESSION_DRAINING,    // shutting down cleanly before returning to idle
};

// Frame budgets differ enormously between two windows on one PC and two
// players across the internet through RetroArch netplay.
enum LinkSessionProfile
{
    LINK_PROFILE_LOCAL,
    LINK_PROFILE_REMOTE,
};

void LinkSession_Init(void);
void LinkSession_SetEnabled(bool8 enabled);
bool8 LinkSession_IsEnabled(void);
void LinkSession_SetProfile(u8 profile);

void LinkSession_Update(void);

u8 LinkSession_GetState(void);
bool8 LinkSession_IsEstablished(void);
u8 LinkSession_GetPeerCount(void);
const struct LinkPlayer *LinkSession_GetPeer(u8 index);

// Barrier/handoff, used to enter a vanilla link activity without reconnecting.
void LinkSession_RequestHandoff(u16 linkType);
bool8 LinkSession_IsHandoffPending(void);
bool8 LinkSession_IsHandoffReady(void);
void LinkSession_BeginHandoff(void);
void LinkSession_EndHandoff(void);
void LinkSession_CancelHandoff(void);

// Destination-map rendezvous: both sides announce they loaded the club
// room before trainer-card exchange. Poll proto only until this returns TRUE.
void LinkSession_NotifyArrived(void);
bool8 LinkSession_PeerHasArrived(void);
bool8 LinkSession_RendezvousComplete(void);
u16 LinkSession_GetRendezvousBudget(void);

#endif // GUARD_LINK_SESSION_H
