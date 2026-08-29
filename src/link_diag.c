#include "global.h"
#include "link.h"
#include "link_diag.h"

static struct LinkDiagStats sStats;
static u32 sPrevErrorBits;

static void Saturate(u16 *counter)
{
    if (*counter != 0xFFFF)
        (*counter)++;
}

void LinkDiag_Reset(void)
{
    memset(&sStats, 0, sizeof(sStats));
    sPrevErrorBits = 0;
}

void LinkDiag_Count(enum LinkDiagEvent event)
{
    if (event < LINK_DIAG_EVENT_COUNT)
        Saturate(&sStats.events[event]);
}

void LinkDiag_SampleLinkStatus(void)
{
    u32 status = gLinkStatus;
    u32 rising = status & ~sPrevErrorBits;
    u32 recvQueue;

    if (rising & LINK_STAT_ERROR_HARDWARE)
        Saturate(&sStats.hardwareErrors);
    if (rising & LINK_STAT_ERROR_CHECKSUM)
        Saturate(&sStats.checksumErrors);
    if (rising & LINK_STAT_ERROR_QUEUE_FULL)
        Saturate(&sStats.queueFullErrors);
    if (rising & (LINK_STAT_ERROR_LAG_MASTER | LINK_STAT_ERROR_LAG_SLAVE))
        Saturate(&sStats.lagErrors);

    sPrevErrorBits = status & LINK_STAT_ERRORS;

    recvQueue = GetLinkRecvQueueLength();
    if (recvQueue > sStats.peakRecvQueue)
        sStats.peakRecvQueue = recvQueue;
}

const struct LinkDiagStats *LinkDiag_GetStats(void)
{
    return &sStats;
}
