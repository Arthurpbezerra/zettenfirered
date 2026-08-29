#ifndef GUARD_LINK_PROTO_H
#define GUARD_LINK_PROTO_H

#include "global.h"
#include "link.h"

// Versioned datagrams on top of SendBlock. No Alloc on this path: the send
// scratch is a small static, and receive uses gBlockRecvBuffer in place.
//
// Every field from a peer is validated (magic, version, channel, length)
// before a handler sees it. Invalid packets are counted and dropped.

#define LINK_PROTO_MAGIC   0xA7E2
#define LINK_PROTO_VERSION 1

enum LinkProtoChannel
{
    LINK_CHAN_NONE = 0,
    LINK_CHAN_CONTROL,
    LINK_CHAN_PRESENCE,
    LINK_CHAN_COOP,
    LINK_CHAN_APP,
    LINK_CHAN_COUNT
};

struct LinkProtoHeader
{
    u16 magic;
    u8 protoVersion;
    u8 channel;
    u8 len;
    u8 seq;
};

#define LINK_PROTO_MAX_PAYLOAD 24

STATIC_ASSERT(sizeof(struct LinkProtoHeader) + LINK_PROTO_MAX_PAYLOAD <= BLOCK_BUFFER_SIZE, LinkProtoFitsBlock);

typedef void (*LinkProtoHandler)(u8 playerId, const u8 *payload, u8 len);

void LinkProto_Reset(void);
void LinkProto_SetHandler(u8 channel, LinkProtoHandler handler);
bool8 LinkProto_Send(u8 channel, const void *payload, u8 len);
void LinkProto_Poll(void);
bool8 LinkProto_HasVersionMismatch(void);
bool8 LinkProto_HasPendingSend(void);

#endif // GUARD_LINK_PROTO_H
