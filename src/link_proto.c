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
    LinkProtoHandler handlers[LINK_CHAN_COUNT];
};

static struct LinkProtoCtx sProto;
static u8 sSendScratch[sizeof(struct LinkProtoHeader) + LINK_PROTO_MAX_PAYLOAD];

STATIC_ASSERT(sizeof(sSendScratch) <= BLOCK_BUFFER_SIZE, LinkProtoScratchFits);

void LinkProto_Reset(void)
{
    u8 i;

    for (i = 0; i < MAX_RFU_PLAYERS; i++)
        sProto.lastSeq[i] = 0;
    sProto.seenMask = 0;
    sProto.nextSeq = 1;
    sProto.versionMismatch = FALSE;
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

bool8 LinkProto_Send(u8 channel, const void *payload, u8 len)
{
    struct LinkProtoHeader *hdr;
    u16 size;

    if (channel <= LINK_CHAN_NONE || channel >= LINK_CHAN_COUNT)
        return FALSE;
    if (payload == NULL && len != 0)
        return FALSE;
    if (len > LINK_PROTO_MAX_PAYLOAD)
        return FALSE;
    if (!IsLinkTaskFinished())
        return FALSE;

    hdr = (struct LinkProtoHeader *)sSendScratch;
    hdr->magic = LINK_PROTO_MAGIC;
    hdr->protoVersion = LINK_PROTO_VERSION;
    hdr->channel = channel;
    hdr->len = len;
    hdr->seq = sProto.nextSeq;
    if (len != 0)
        memcpy(sSendScratch + sizeof(*hdr), payload, len);

    size = sizeof(*hdr) + len;
    if (!SendBlock(0, sSendScratch, size))
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

void LinkProto_Poll(void)
{
    u8 status = GetBlockReceivedStatus();
    u8 i;
    u8 selfId = GetMultiplayerId();
    const struct LinkProtoHeader *hdr;
    const u8 *raw;
    u8 len;

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
