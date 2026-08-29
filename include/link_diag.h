#ifndef GUARD_LINK_DIAG_H
#define GUARD_LINK_DIAG_H

#include "global.h"

// Counters for the custom link stack. The vanilla error UI is suppressed while
// a session is running, so these are the only record of why a link degraded.
// Everything here is a saturating u16: a counter that stops at 0xFFFF is more
// useful than one that wraps and lies.

enum LinkDiagEvent
{
    LINK_DIAG_SESSION_OPENED,
    LINK_DIAG_SESSION_ESTABLISHED,
    LINK_DIAG_SESSION_DROPPED,
    LINK_DIAG_TIMEOUT,
    LINK_DIAG_PKT_SENT,
    LINK_DIAG_PKT_SEND_FAILED,
    LINK_DIAG_PKT_RECV,
    LINK_DIAG_PKT_BAD_MAGIC,
    LINK_DIAG_PKT_BAD_VERSION,
    LINK_DIAG_PKT_BAD_CHANNEL,
    LINK_DIAG_PKT_BAD_LENGTH,
    LINK_DIAG_PKT_DUPLICATE,
    LINK_DIAG_PEER_SANITIZED,
    LINK_DIAG_EVENT_COUNT
};

struct LinkDiagStats
{
    u16 events[LINK_DIAG_EVENT_COUNT];
    u16 hardwareErrors;
    u16 checksumErrors;
    u16 queueFullErrors;
    u16 lagErrors;
    u16 peakRecvQueue;
};

void LinkDiag_Reset(void);
void LinkDiag_Count(enum LinkDiagEvent event);

// Call once per frame while the link is open. Edge-detects the error bits in
// gLinkStatus so a stuck error bit counts once instead of once per frame.
void LinkDiag_SampleLinkStatus(void);

const struct LinkDiagStats *LinkDiag_GetStats(void);

#endif // GUARD_LINK_DIAG_H
