#include "global.h"
#include "gflib.h"
#include "link.h"
#include "link_diag.h"
#include "link_proto.h"

struct LinkProtoCtx
{
    u8 lastSeq[MAX_RFU_PLAYERS];
    u8 seenMask;
    u8 nextSeq;
    bool8 versionMismatch;
    bool8 pending;
    u8 pendingSize;
    u8 pendingChannel;
    LinkProtoHandler handlers[LINK_CHAN_COUNT];
};

static struct LinkProtoCtx sProto;
static u8 sSendScratch[sizeof(struct LinkProtoHeader) + LINK_PROTO_MAX_PAYLOAD];
static u8 sPendingScratch[sizeof(struct LinkProtoHeader) + LINK_PROTO_MAX_PAYLOAD];

STATIC_ASSERT(sizeof(sSendScratch) <= BLOCK_BUFFER_SIZE, LinkProtoScratchFits);
STATIC_ASSERT(sizeof(sPendingScratch) <= BLOCK_BUFFER_SIZE, LinkProtoPendingFits);

static bool8 ChannelYieldsToPending(u8 channel)
{
    return channel == LINK_CHAN_PRESENCE || channel == LINK_CHAN_COOP;
}

static void ClearPending(void)
{
    sProto.pending = FALSE;
    sProto.pendingSize = 0;
    sProto.pendingChannel = LINK_CHAN_NONE;
}

static bool8 Transmit(const u8 *buf, u16 size)
{
    if (!SendBlock(0, buf, size))
    {
        LinkDiag_Count(LINK_DIAG_PKT_SEND_FAILED);
        return FALSE;
    }
    sProto.nextSeq++;
    if (sProto.nextSeq == 0)
        sProto.nextSeq = 1;
    LinkDiag_Count(LINK_DIAG_PKT_SENT);
    return TRUE;
}

static void BuildPacket(u8 *dest, u8 channel, const void *payload, u8 len)
{
    struct LinkProtoHeader *hdr = (struct LinkProtoHeader *)dest;

    hdr->magic = LINK_PROTO_MAGIC;
    hdr->protoVersion = LINK_PROTO_VERSION;
    hdr->channel = channel;
    hdr->len = len;
    hdr->seq = sProto.nextSeq;
    if (len != 0)
        memcpy(dest + sizeof(*hdr), payload, len);
}

static bool8 FlushPending(void)
{
    struct LinkProtoHeader *hdr;

    if (!sProto.pending)
        return TRUE;
    if (!IsLinkTaskFinished())
        return FALSE;

    hdr = (struct LinkProtoHeader *)sPendingScratch;
    hdr->seq = sProto.nextSeq;
    if (!Transmit(sPendingScratch, sProto.pendingSize))
        return FALSE;
    ClearPending();
    return TRUE;
}

void LinkProto_Reset(void)
{
    u8 i;

    for (i = 0; i < MAX_RFU_PLAYERS; i++)
        sProto.lastSeq[i] = 0;
    sProto.seenMask = 0;
    sProto.nextSeq = 1;
    sProto.versionMismatch = FALSE;
    ClearPending();
}

void LinkProto_SetHandler(u8 channel, LinkProtoHandler handler)
{
    if (channel > LINK_CHAN_NONE && channel < LINK_CHAN_COUNT)
        sProto.handlers[channel] = handler;
}

bool8 LinkProto_HasVersionMismatch(void)
{
    return sProto.versionMismatch;
}

bool8 LinkProto_HasPendingSend(void)
{
    return sProto.pending;
}

bool8 LinkProto_Send(u8 channel, const void *payload, u8 len)
{
    u16 size;

    if (channel <= LINK_CHAN_NONE || channel >= LINK_CHAN_COUNT)
        return FALSE;
    if (payload == NULL && len != 0)
        return FALSE;
    if (len > LINK_PROTO_MAX_PAYLOAD)
        return FALSE;

    size = sizeof(struct LinkProtoHeader) + len;
    FlushPending();

    if (ChannelYieldsToPending(channel))
    {
        if (sProto.pending || !IsLinkTaskFinished())
            return FALSE;
        BuildPacket(sSendScratch, channel, payload, len);
        return Transmit(sSendScratch, size);
    }

    // Control/app: if the wire is busy, keep one datagram and retry from Poll.
    if (IsLinkTaskFinished())
    {
        BuildPacket(sSendScratch, channel, payload, len);
        if (Transmit(sSendScratch, size))
            return TRUE;
    }

    if (sProto.pending && sProto.pendingChannel == LINK_CHAN_CONTROL
     && channel != LINK_CHAN_CONTROL)
        return FALSE;

    BuildPacket(sPendingScratch, channel, payload, len);
    sProto.pendingSize = size;
    sProto.pendingChannel = channel;
    sProto.pending = TRUE;
    return TRUE;
}

void LinkProto_Poll(void)
{
    u8 status;
    u8 i;
    u8 selfId = GetMultiplayerId();
    const struct LinkProtoHeader *hdr;
    const u8 *raw;
    u8 len;

    FlushPending();

    status = GetBlockReceivedStatus();
    if (status == 0)
        return;

    for (i = 0; i < MAX_RFU_PLAYERS; i++)
    {
        if (i == selfId || !((status >> i) & 1))
            continue;

        raw = (const u8 *)gBlockRecvBuffer[i];
        hdr = (const struct LinkProtoHeader *)raw;

        if (hdr->magic != LINK_PROTO_MAGIC)
        {
            LinkDiag_Count(LINK_DIAG_PKT_BAD_MAGIC);
            ResetBlockReceivedFlag(i);
            continue;
        }
        if (hdr->protoVersion != LINK_PROTO_VERSION)
        {
            sProto.versionMismatch = TRUE;
            LinkDiag_Count(LINK_DIAG_PKT_BAD_VERSION);
            ResetBlockReceivedFlag(i);
            continue;
        }
        if (hdr->channel <= LINK_CHAN_NONE || hdr->channel >= LINK_CHAN_COUNT)
        {
            LinkDiag_Count(LINK_DIAG_PKT_BAD_CHANNEL);
            ResetBlockReceivedFlag(i);
            continue;
        }
        len = hdr->len;
        if (len > LINK_PROTO_MAX_PAYLOAD)
        {
            LinkDiag_Count(LINK_DIAG_PKT_BAD_LENGTH);
            ResetBlockReceivedFlag(i);
            continue;
        }

        if (sProto.seenMask & (1 << i))
        {
            if (hdr->seq == sProto.lastSeq[i])
            {
                LinkDiag_Count(LINK_DIAG_PKT_DUPLICATE);
                ResetBlockReceivedFlag(i);
                continue;
            }
        }
        else
        {
            sProto.seenMask |= (1 << i);
        }
        sProto.lastSeq[i] = hdr->seq;

        LinkDiag_Count(LINK_DIAG_PKT_RECV);
        if (sProto.handlers[hdr->channel] != NULL)
            sProto.handlers[hdr->channel](i, raw + sizeof(*hdr), len);

        ResetBlockReceivedFlag(i);
    }
}
