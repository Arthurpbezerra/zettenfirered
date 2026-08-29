#include "global.h"
#include "gflib.h"
#include "characters.h"
#include "phone.h"
#include "battle.h"
#include "event_data.h"
#include "item.h"
#include "item_menu.h"
#include "link.h"
#include "link_diag.h"
#include "link_proto.h"
#include "link_coop.h"
#include "link_session.h"
#include "list_menu.h"
#include "menu.h"
#include "new_menu_helpers.h"
#include "overworld.h"
#include "safari_zone.h"
#include "script.h"
#include "event_scripts.h"
#include "field_fadetransition.h"
#include "field_weather.h"
#include "help_system.h"
#include "global.fieldmap.h"
#include "palette.h"
#include "scanline_effect.h"
#include "sound.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "trade.h"
#include "trainer_card.h"
#include "window.h"
#include "pokemon.h"
#include "script_pokemon_util.h"
#include "save.h"
#include "constants/cable_club.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/map_types.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/vars.h"
#include "constants/maps.h"
#include "load_save.h"

#define WIN_HEADER 0
#define WIN_LIST   1
#define WIN_FOOTER 2
#define WIN_ACTION 3

#define MAX_SHOWED 7
#define LABEL_WIDTH 28

enum {
    AGENDA_STATE_MAIN,
    AGENDA_STATE_CONFIRM_REMOVE,
    AGENDA_STATE_ACTIONS,
    AGENDA_STATE_WAIT_REPLY,
    AGENDA_STATE_INCOMING,
    AGENDA_STATE_FLUSH_ACCEPT,
    AGENDA_STATE_DECLINED,
    AGENDA_STATE_DIAG,
};

enum {
    PHONE_MSG_NONE = 0,
    PHONE_MSG_BATTLE = 1,
    PHONE_MSG_TRADE = 2,
    PHONE_MSG_ACCEPT = 3,
    PHONE_MSG_DECLINE = 4,
};

struct PhoneAppMsg
{
    u8 kind;
    u8 pad[3];
    u32 trainerId;
};

STATIC_ASSERT(sizeof(struct PhoneAppMsg) <= LINK_PROTO_MAX_PAYLOAD, PhoneAppMsgFitsProto);

enum {
    PHONE_PENDING_NONE,
    PHONE_PENDING_BATTLE,
    PHONE_PENDING_TRADE,
};

enum {
    PHONE_ACTION_BATTLE,
    PHONE_ACTION_TRADE,
    PHONE_ACTION_REMOVE,
    PHONE_ACTION_CANCEL,
    PHONE_ACTION_COUNT,
};

// One allocation owns every buffer the Agenda screen needs. The list items and
// their label strings used to be permanently resident EWRAM statics even though
// they are meaningless while the screen is closed.
struct PhoneAgenda
{
    MainCallback savedCallback;
    u8 loadState;
    u8 uiState;
    u8 listTaskId;
    u8 numItems;
    u8 selectedSlot;
    struct ListMenuItem items[PHONE_MAX_CONTACTS];
    u8 labels[PHONE_MAX_CONTACTS][LABEL_WIDTH];
};

static EWRAM_DATA struct PhoneAgenda *sAgenda = NULL;
static EWRAM_DATA u32 sPhoneOnlineIds[MAX_LINK_PLAYERS] = {0};
static EWRAM_DATA u8 sPhoneOnlineCount = 0;
static EWRAM_DATA bool8 sPhoneLinkConnected = FALSE;
static EWRAM_DATA u16 sAgendaSeenLink = 0xFFFF;
static EWRAM_DATA u8 sPendingPhoneActivity = 0;
static EWRAM_DATA bool8 sPhoneClubBusy = FALSE;
static EWRAM_DATA bool8 sPhoneStayOnField = FALSE;
static EWRAM_DATA u8 sOutgoingKind = 0;
static EWRAM_DATA u8 sIncomingKind = 0;
static EWRAM_DATA u32 sIncomingTrainerId = 0;
static EWRAM_DATA bool8 sWaitingReply = FALSE;
static EWRAM_DATA u8 sFlushDelay = 0;

static void CB2_PhoneAgenda(void);
static void VBlankCB_PhoneAgenda(void);
static void Task_PhoneAgenda(u8 taskId);
static void Task_ClosePhoneAgenda(u8 taskId);
static void Phone_SeedDummyContacts(void);
static void Phone_CopyPeerName(u8 *dest, const u8 *src);
static void Phone_ShowDiagnostics(void);
static void Phone_DestroyList(void);
static void Phone_BuildAgendaList(void);
static void Phone_PrintHeader(void);
static void Phone_PrintFooter(const u8 *str);
static void Phone_BeginRemoveConfirm(u32 contactSlot);
static void Phone_HandleRemoveConfirm(u8 taskId);
static void Phone_BeginActionMenu(u32 contactSlot);
static void Phone_DestroyActionWindow(void);
static void Phone_HandleActionMenu(u8 taskId);
static void Phone_QueueClubActivity(u8 taskId, u8 kind);
static void Phone_FieldCB_ReturnForLinkup(void);
static void Task_PhoneClubLinkup(u8 taskId);
static void Phone_ClearOnlinePeers(void);
static void Phone_SyncOnlinePeers(void);
static void Phone_RefreshAgendaIfNeeded(void);
static u32 Phone_GetPlayerTrainerId(void);
static bool8 Phone_TrySendMsg(u8 kind);
static void Phone_OnAppPacket(u8 playerId, const u8 *payload, u8 len);
static void Phone_PollLinkMsgs(void);
static void Phone_BeginIncomingPrompt(void);
static void Phone_HandleIncomingConfirm(u8 taskId);
static void Phone_HandleFlushAccept(u8 taskId);
static void Phone_HandleWaitReply(u8 taskId);
static void Task_OpenAgendaIncoming(u8 taskId);
static void Phone_TryOpenAgendaForIncoming(void);
static bool8 Phone_PartyCanTrade(void);
static const u8 *Phone_TradeBlockReason(void);
static void Phone_EnterOverworldTrade(void);
static void Phone_ApplyClubLinkType(void);

static const u8 sText_AgendaTitle[] = _("TRAINER AGENDA");
static const u8 sText_FooterMain[] = _("A: ACTION  B: EXIT  SELECT: DIAG");
static const u8 sText_FooterDiag[] = _("SELECT / B: BACK");
static const u8 sText_DiagSessions[] = _("SESSION open/ok/lost");
static const u8 sText_DiagPackets[] = _("PACKET sent/recv/fail");
static const u8 sText_DiagRejected[] = _("REJECT magic/ver/kind");
static const u8 sText_DiagErrors[] = _("ERROR hw/sum/queue/lag");
static const u8 sText_DiagMisc[] = _("TIMEOUT/SANITIZED/PEAKQ");
static const u8 sText_FooterConfirm[] = _("Remove this contact?");
static const u8 sText_FooterActions[] = _("A: CHOOSE  B: BACK");
static const u8 sText_FooterWaiting[] = _("WAITING  B: CANCEL");
static const u8 sText_IncomingBattle[] = _("Battle request. Accept?");
static const u8 sText_IncomingTrade[] = _("Trade request. Accept?");
static const u8 sText_RomMismatch[] = _("Partner ROM is a different version.");
static const u8 sText_RequestDeclined[] = _("The other TRAINER declined.");
static const u8 sText_NeedTwoMons[] = _("NEED 2 POKéMON TO TRADE.");
static const u8 sText_CantEnigma[] = _("CAN'T TRADE ENIGMA BERRY.");
static const u8 sText_ActionBattle[] = _("BATTLE");
static const u8 sText_ActionTrade[] = _("TRADE");
static const u8 sText_ActionRemove[] = _("REMOVE");
static const u8 sText_ActionCancel[] = _("CANCEL");
static const u8 sText_NoContacts[] = _("No contacts saved.$");
static const u8 sText_Offline[] = _("  OFF");
static const u8 sText_Online[] = _("  ON");
static const u8 sText_On[] = _("  ON");
static const u8 sText_Off[] = _("  OFF");
static const u8 sText_IdLabel[] = _("  ID:");
static const u8 sText_DummyRed[] = _("RED");
static const u8 sText_DummyBlue[] = _("BLUE");
static const u8 sText_DummyGreen[] = _("GREEN");

static const u8 sMenuTextColor[] = {TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};

static const u16 sBlackPal[16] = {0};

static const struct BgTemplate sAgendaBgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    }
};

static const struct WindowTemplate sAgendaWinTemplates[] =
{
    {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 1,
        .width = 26,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 1
    },
    {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 6,
        .width = 26,
        .height = 10,
        .paletteNum = 15,
        .baseBlock = 105
    },
    {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 18,
        .width = 26,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 365
    },
    {
        .bg = 0,
        .tilemapLeft = 19,
        .tilemapTop = 7,
        .width = 8,
        .height = 8,
        .paletteNum = 14,
        .baseBlock = 417
    },
    DUMMY_WIN_TEMPLATE
};

static const struct WindowTemplate sYesNoTemplate =
{
    .bg = 0,
    .tilemapLeft = 21,
    .tilemapTop = 9,
    .width = 6,
    .height = 4,
    .paletteNum = 14,
    .baseBlock = 481
};

static const struct MenuAction sPhoneActions[] =
{
    {sText_ActionBattle, {NULL}},
    {sText_ActionTrade, {NULL}},
    {sText_ActionRemove, {NULL}},
    {sText_ActionCancel, {NULL}},
};

static const u8 sPhoneActionOrder[] = {
    PHONE_ACTION_BATTLE,
    PHONE_ACTION_TRADE,
    PHONE_ACTION_REMOVE,
    PHONE_ACTION_CANCEL,
};

static const struct ListMenuTemplate sAgendaListTemplate =
{
    .items = NULL,
    .moveCursorFunc = ListMenuDefaultCursorMoveFunc,
    .itemPrintFunc = NULL,
    .totalItems = 0,
    .maxShowed = MAX_SHOWED,
    .windowId = WIN_LIST,
    .header_X = 0,
    .item_X = 8,
    .cursor_X = 0,
    .upText_Y = 1,
    .cursorPal = 2,
    .fillValue = 1,
    .cursorShadowPal = 3,
    .lettersSpacing = 0,
    .itemVerticalPadding = 0,
    .scrollMultiple = LIST_NO_MULTIPLE_SCROLL,
    .fontId = FONT_NORMAL,
    .cursorKind = 0
};

void Phone_InitSave(void)
{
    memset(&gSaveBlock2Ptr->phone, 0, sizeof(struct PhoneSaveData));
    gSaveBlock2Ptr->phone.magic = PHONE_SAVE_MAGIC;
    Phone_SeedDummyContacts();
    gSaveBlock2Ptr->phone.dummySeeded = TRUE;
}

void Phone_EnsureReady(void)
{
    u32 i;

    if (gSaveBlock2Ptr->phone.magic != PHONE_SAVE_MAGIC)
        Phone_InitSave();
    else
    {
        // A save written by an older build could hold an unterminated name.
        for (i = 0; i < PHONE_MAX_CONTACTS; i++)
            gSaveBlock2Ptr->phone.contacts[i].name[PLAYER_NAME_LENGTH] = EOS;
    }

    LinkProto_SetHandler(LINK_CHAN_APP, Phone_OnAppPacket);
}

static void Phone_SeedDummyContacts(void)
{
    Phone_AddContact(sText_DummyRed, 38427, MALE, PHONE_CONTACT_DUMMY);
    Phone_AddContact(sText_DummyBlue, 52891, MALE, PHONE_CONTACT_DUMMY);
    Phone_AddContact(sText_DummyGreen, 10234, FEMALE, PHONE_CONTACT_DUMMY);
}

s32 Phone_FindByTrainerId(u32 trainerId)
{
    u32 i;
    u16 disp;

    Phone_EnsureReady();
    if (trainerId == 0)
        return -1;
    disp = (u16)trainerId;
    for (i = 0; i < PHONE_MAX_CONTACTS; i++)
    {
        if (gSaveBlock2Ptr->phone.contacts[i].trainerId == 0)
            continue;
        if (gSaveBlock2Ptr->phone.contacts[i].trainerId == trainerId)
            return i;
        if ((u16)gSaveBlock2Ptr->phone.contacts[i].trainerId == disp)
            return i;
    }
    return -1;
}

// Contact names can originate from a peer over the link, where nothing
// guarantees a terminator. An unbounded copy here writes past the 8-byte field
// and into the rest of SaveBlock2.
static void Phone_CopyPeerName(u8 *dest, const u8 *src)
{
    u32 i;

    for (i = 0; i < PLAYER_NAME_LENGTH && src[i] != EOS; i++)
        dest[i] = src[i];
    dest[i] = EOS;

    if (i == PLAYER_NAME_LENGTH && src[i] != EOS)
        LinkDiag_Count(LINK_DIAG_PEER_SANITIZED);
}

bool8 Phone_AddContact(const u8 *name, u32 trainerId, u8 gender, u8 flags)
{
    u32 i;
    s32 existing;

    Phone_EnsureReady();
    if (trainerId == 0)
        return FALSE;
    existing = Phone_FindByTrainerId(trainerId);
    if (existing >= 0)
    {
        if (gSaveBlock2Ptr->phone.contacts[existing].trainerId != trainerId)
            gSaveBlock2Ptr->phone.contacts[existing].trainerId = trainerId;
        return FALSE;
    }

    for (i = 0; i < PHONE_MAX_CONTACTS; i++)
    {
        if (gSaveBlock2Ptr->phone.contacts[i].trainerId == 0)
        {
            Phone_CopyPeerName(gSaveBlock2Ptr->phone.contacts[i].name, name);
            gSaveBlock2Ptr->phone.contacts[i].trainerId = trainerId;
            gSaveBlock2Ptr->phone.contacts[i].gender = gender;
            gSaveBlock2Ptr->phone.contacts[i].flags = flags;
            return TRUE;
        }
    }
    return FALSE;
}

void Phone_RemoveContact(u32 index)
{
    Phone_EnsureReady();
    if (index < PHONE_MAX_CONTACTS)
        memset(&gSaveBlock2Ptr->phone.contacts[index], 0, sizeof(struct PhoneContact));
}

u32 Phone_CountContacts(void)
{
    u32 i, count = 0;

    Phone_EnsureReady();
    for (i = 0; i < PHONE_MAX_CONTACTS; i++)
    {
        if (gSaveBlock2Ptr->phone.contacts[i].trainerId != 0)
            count++;
    }
    return count;
}

u16 Phone_GetDisplayId(u32 trainerId)
{
    return (u16)trainerId;
}

u16 Phone_GetPlayerDisplayId(void)
{
    return (gSaveBlock2Ptr->playerTrainerId[1] << 8) | gSaveBlock2Ptr->playerTrainerId[0];
}

const struct PhoneContact *Phone_GetContact(u32 index)
{
    Phone_EnsureReady();
    if (index >= PHONE_MAX_CONTACTS)
        return NULL;
    return &gSaveBlock2Ptr->phone.contacts[index];
}

void GivePhoneKeyItems(void)
{
    if (!CheckBagHasItem(ITEM_CONNECTOR, 1))
        AddBagItem(ITEM_CONNECTOR, 1);
    if (!CheckBagHasItem(ITEM_TRAINER_AGENDA, 1))
        AddBagItem(ITEM_TRAINER_AGENDA, 1);
    FlagSet(FLAG_GOT_PHONE_GEAR);
    Phone_EnsureReady();
}

static void Phone_ClearOnlinePeers(void)
{
    s32 i;

    for (i = 0; i < MAX_LINK_PLAYERS; i++)
        sPhoneOnlineIds[i] = 0;
    sPhoneOnlineCount = 0;
    sPhoneLinkConnected = FALSE;
}

bool8 Phone_IsTrainerOnline(u32 trainerId)
{
    u8 i;
    u16 disp;

    if (trainerId == 0 || !sPhoneLinkConnected)
        return FALSE;
    disp = (u16)trainerId;
    for (i = 0; i < sPhoneOnlineCount; i++)
    {
        if (sPhoneOnlineIds[i] == trainerId || (u16)sPhoneOnlineIds[i] == disp)
            return TRUE;
    }
    return FALSE;
}

bool8 Phone_IsLinkConnected(void)
{
    return sPhoneLinkConnected;
}

// Mirrors the session's peer list into the Agenda's view and records anyone
// new as a contact. Peer names are untrusted; Phone_AddContact bounds them.
static void Phone_SyncOnlinePeers(void)
{
    u8 count = LinkSession_GetPeerCount();
    u8 i;

    Phone_ClearOnlinePeers();
    if (!LinkSession_IsEstablished())
        return;

    for (i = 0; i < count && i < MAX_LINK_PLAYERS; i++)
    {
        const struct LinkPlayer *peer = LinkSession_GetPeer(i);

        if (peer == NULL || peer->trainerId == 0)
            continue;
        sPhoneOnlineIds[sPhoneOnlineCount++] = peer->trainerId;
        Phone_AddContact(peer->name, peer->trainerId, peer->gender, 0);
    }
    sPhoneLinkConnected = (sPhoneOnlineCount != 0);
}

void Phone_UpdateConnector(void)
{
    bool8 on = FlagGet(FLAG_SYS_CONNECTOR_ON);

    LinkSession_SetEnabled(on);
    if (!on)
        Phone_ClearOnlinePeers();
}

static bool8 WarpIsCableClubRoom(s8 mapGroup, s8 mapNum)
{
    u16 map = (mapNum & 0xFF) | ((mapGroup & 0xFF) << 8);

    return map == MAP_TRADE_CENTER
        || map == MAP_BATTLE_COLOSSEUM_2P
        || map == MAP_BATTLE_COLOSSEUM_4P
        || map == MAP_UNION_ROOM
        || map == MAP_RECORD_CORNER
        || map == MAP_TWO_ISLAND_JOYFUL_GAME_CORNER;
}

static bool8 MapIsCableClubRoom(void)
{
    return WarpIsCableClubRoom(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
}

// Link maps live in group 0 starting at (0,0)=Colosseum. A never-written
// dynamicWarp is all zeros, so a vanilla trade-save continue warp would dump
// the player in the Colosseum after an overworld trade.
void Phone_IgnoreContinueWarpIntoClub(void)
{
    if (!UseContinueGameWarp())
        return;
    if (!WarpIsCableClubRoom(gSaveBlock1Ptr->continueGameWarp.mapGroup,
                             gSaveBlock1Ptr->continueGameWarp.mapNum))
        return;
    ClearContinueGameWarpStatus();
}

void Phone_CommitOverworldReturn(void)
{
    Phone_SaveReturnWarp();
    ClearContinueGameWarpStatus();
    Phone_OnClubLinkupEnd();
    TrySavingData(SAVE_NORMAL);
}

// VAR_CABLE_CLUB_STATE lives in the save. A failed club warp can leave it
// non-zero forever. Repair only when we are clearly not in a club room and
// not mid-handoff.
static void Phone_RepairStaleClubState(void)
{
    if (LinkSession_GetState() == LINK_SESSION_HANDOFF)
        return;
    if (gReceivedRemoteLinkPlayers && gLinkType != LINKTYPE_PHONE)
        return;
    if (MapIsCableClubRoom())
        return;
    if (VarGet(VAR_CABLE_CLUB_STATE) == 0 && !sPhoneClubBusy)
        return;
    VarSet(VAR_CABLE_CLUB_STATE, 0);
    sPhoneClubBusy = FALSE;
}

void Phone_OnClubLinkupEnd(void)
{
    sPhoneClubBusy = FALSE;
    sPhoneStayOnField = FALSE;
    sPendingPhoneActivity = PHONE_PENDING_NONE;
    sOutgoingKind = 0;
    sIncomingKind = 0;
    sWaitingReply = FALSE;
    VarSet(VAR_CABLE_CLUB_STATE, 0);
    LinkSession_EndHandoff();
    SetMainCallback1(CB1_Overworld);
    HelpSystem_Enable();
}

bool8 Phone_IsClubSessionActive(void)
{
    return sPhoneClubBusy;
}

bool8 Phone_ShouldReturnToCurrentField(void)
{
    return sPhoneStayOnField;
}

static bool8 Phone_PartyCanTrade(void)
{
    if (CalculatePlayerPartyCount() < 2)
        return FALSE;
    if (DoesPartyHaveEnigmaBerry() == TRUE)
        return FALSE;
    return TRUE;
}

static const u8 *Phone_TradeBlockReason(void)
{
    if (CalculatePlayerPartyCount() < 2)
        return sText_NeedTwoMons;
    return sText_CantEnigma;
}

void Phone_StartOverworldTrade(void)
{
    Phone_EnterOverworldTrade();
}

static void Phone_EnterOverworldTrade(void)
{
    sPhoneStayOnField = TRUE;
    sPhoneClubBusy = TRUE;
    sPendingPhoneActivity = PHONE_PENDING_NONE;
    sOutgoingKind = 0;
    sIncomingKind = 0;
    sWaitingReply = FALSE;

    if (LinkSession_IsHandoffReady())
        LinkSession_BeginHandoff();

    Phone_SaveReturnWarp();

    gSpecialVar_0x8004 = USING_TRADE_CENTER;
    gLinkType = LINKTYPE_TRADE_SETUP;
    gBattleTypeFlags = 0;
    gFieldLinkPlayerCount = GetLinkPlayerCount_2();
    gLocalLinkPlayerId = GetMultiplayerId();
    if (gFieldLinkPlayerCount >= 2)
        SaveLinkPlayers(gFieldLinkPlayerCount);

    SetSuppressLinkErrorMessage(TRUE);
    HelpSystem_Disable();
    SetMainCallback1(CB1_Overworld);
    SetMainCallback2(CB2_StartCreateTradeMenu);
}

void Phone_SaveReturnWarp(void)
{
    SetDynamicWarp(0,
        gSaveBlock1Ptr->location.mapGroup,
        gSaveBlock1Ptr->location.mapNum,
        WARP_ID_NONE);
}

void Phone_ShouldStartRoomLinkup(void)
{
    gSpecialVar_Result = sPhoneClubBusy;
}

void Phone_WarpToReturnPoint(void)
{
    SetWarpDestinationToDynamicWarp(0);
    DoWarp();
}

static void Phone_FieldCB_ReturnForLinkup(void)
{
    FadeInFromBlack();
}

void Phone_TryStartPendingLinkup(void)
{
    if (sPendingPhoneActivity == PHONE_PENDING_NONE)
        return;
    if (!Overworld_IsFieldCB2Active())
        return;
    if (gPaletteFade.active)
        return;
    if (ScriptContext_IsEnabled())
        return;

    if (sPendingPhoneActivity == PHONE_PENDING_TRADE)
    {
        sPendingPhoneActivity = PHONE_PENDING_NONE;
        Phone_EnterOverworldTrade();
        return;
    }
    ScriptContext_SetupScript(EventScript_PhoneTryBattle);
    sPendingPhoneActivity = PHONE_PENDING_NONE;
}

static u32 Phone_GetPlayerTrainerId(void)
{
    return T1_READ_32(gSaveBlock2Ptr->playerTrainerId);
}

static bool8 Phone_TrySendMsg(u8 kind)
{
    struct PhoneAppMsg msg;

    if (!sPhoneLinkConnected)
        return FALSE;
    if (!LinkSession_IsEstablished())
        return FALSE;

    memset(&msg, 0, sizeof(msg));
    msg.kind = kind;
    msg.trainerId = Phone_GetPlayerTrainerId();
    return LinkProto_Send(LINK_CHAN_APP, &msg, sizeof(msg));
}

static void Phone_OnAppPacket(u8 playerId, const u8 *payload, u8 len)
{
    const struct PhoneAppMsg *msg;

    (void)playerId;
    if (len < sizeof(*msg))
    {
        LinkDiag_Count(LINK_DIAG_PKT_BAD_LENGTH);
        return;
    }
    msg = (const struct PhoneAppMsg *)payload;
    if (msg->kind == PHONE_MSG_NONE || msg->kind > PHONE_MSG_DECLINE)
    {
        LinkDiag_Count(LINK_DIAG_PKT_BAD_CHANNEL);
        return;
    }

    switch (msg->kind)
    {
    case PHONE_MSG_BATTLE:
    case PHONE_MSG_TRADE:
        if (sWaitingReply && sOutgoingKind == msg->kind)
        {
            sWaitingReply = FALSE;
            sIncomingKind = 0;
            sPendingPhoneActivity = (msg->kind == PHONE_MSG_TRADE) ? PHONE_PENDING_TRADE : PHONE_PENDING_BATTLE;
        }
        else
        {
            sIncomingKind = msg->kind;
            sIncomingTrainerId = msg->trainerId;
        }
        break;
    case PHONE_MSG_ACCEPT:
        if (sWaitingReply)
        {
            sWaitingReply = FALSE;
            sPendingPhoneActivity = (sOutgoingKind == PHONE_MSG_TRADE) ? PHONE_PENDING_TRADE : PHONE_PENDING_BATTLE;
        }
        break;
    case PHONE_MSG_DECLINE:
        sWaitingReply = FALSE;
        sIncomingKind = PHONE_MSG_DECLINE;
        break;
    }
}

static void Phone_PollLinkMsgs(void)
{
    if (!LinkSession_IsEstablished())
        return;
    LinkProto_Poll();
}

static void Task_OpenAgendaIncoming(u8 taskId)
{
    if (gPaletteFade.active)
        return;
    CleanupOverworldWindowsAndTilemaps();
    ShowPhoneAgenda(CB2_ReturnToField);
    DestroyTask(taskId);
}

static void Phone_TryOpenAgendaForIncoming(void)
{
    if (sIncomingKind != PHONE_MSG_BATTLE && sIncomingKind != PHONE_MSG_TRADE)
        return;
    if (sAgenda != NULL)
        return;
    if (!Overworld_IsFieldCB2Active())
        return;
    if (gPaletteFade.active)
        return;
    if (ArePlayerFieldControlsLocked())
        return;
    if (FuncIsActiveTask(Task_OpenAgendaIncoming))
        return;
    LockPlayerFieldControls();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    CreateTask(Task_OpenAgendaIncoming, 80);
}

// Single per-frame entry point for the phone stack from the overworld.
void Phone_TryResumeLink(void)
{
    Phone_RepairStaleClubState();
    Phone_TryOpenAgendaForIncoming();

    LinkSession_SetEnabled(FlagGet(FLAG_SYS_CONNECTOR_ON)
                        && !sPhoneClubBusy
                        && sPendingPhoneActivity == PHONE_PENDING_NONE);
    LinkSession_Update();
    Phone_SyncOnlinePeers();

    if (LinkSession_IsEstablished())
        Phone_PollLinkMsgs();
    LinkCoop_Update();
}

static void Phone_FinishClubLinkup(u8 taskId, u16 result)
{
    gSpecialVar_Result = result;
    SetSuppressLinkErrorMessage(TRUE);
    if (result != LINKUP_SUCCESS)
    {
        LinkSession_EndHandoff();
        SetMainCallback1(CB1_Overworld);
        ScriptContext_SetupScript(EventScript_PhoneClubLinkupFailed);
    }
    else
    {
        Overworld_InitCableClubAfterLinkup();
    }
    ScriptContext_Enable();
    DestroyTask(taskId);
}

static u16 Phone_GetClubLinkType(void)
{
    if (gSpecialVar_0x8004 == USING_TRADE_CENTER)
        return LINKTYPE_TRADE_SETUP;
    return LINKTYPE_SINGLE_BATTLE;
}

static void Phone_ApplyClubLinkType(void)
{
    u8 i;
    u8 n;
    u16 type = Phone_GetClubLinkType();

    gLinkType = type;
    n = GetLinkPlayerCount();
    for (i = 0; i < n && i < MAX_LINK_PLAYERS; i++)
        gLinkPlayers[i].linkType = type;
}

void TryPhoneClubLinkup(void)
{
    gSpecialVar_Result = LINKUP_ONGOING;
    SetSuppressLinkErrorMessage(TRUE);
    Phone_ApplyClubLinkType();
    if (gLinkType == LINKTYPE_TRADE_SETUP)
        gBattleTypeFlags = 0;
    CreateTask(Task_PhoneClubLinkup, 80);
}

enum {
    CLUB_HANDOFF_WAIT_LINK,
    CLUB_HANDOFF_ARRIVE,
    CLUB_HANDOFF_APPLY,
    CLUB_HANDOFF_SEND_CARD,
    CLUB_HANDOFF_WAIT_CARD,
};

static void Task_PhoneClubLinkup(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 i;
    u16 budget = LinkSession_GetRendezvousBudget();

    switch (data[0])
    {
    case CLUB_HANDOFF_WAIT_LINK:
        // Drain leftover presence packets so they cannot be mistaken for
        // trainer cards, and wait until the partner has finished loading.
        LinkProto_Poll();
        if (HasLinkErrorOccurred() == TRUE)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_CONNECTION_ERROR);
            break;
        }
        if (gReceivedRemoteLinkPlayers && GetLinkPlayerCount_2() >= 2)
        {
            data[1] = 0;
            data[0] = CLUB_HANDOFF_ARRIVE;
        }
        else if (++data[1] > budget)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_FAILED);
        }
        break;
    case CLUB_HANDOFF_ARRIVE:
        LinkProto_Poll();
        LinkSession_NotifyArrived();
        if (HasLinkErrorOccurred() == TRUE)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_CONNECTION_ERROR);
            break;
        }
        if (LinkSession_RendezvousComplete())
        {
            data[1] = 0;
            data[0] = CLUB_HANDOFF_APPLY;
        }
        else if (++data[1] > budget)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_FAILED);
        }
        break;
    case CLUB_HANDOFF_APPLY:
        if (!gReceivedRemoteLinkPlayers || GetLinkPlayerCount_2() < 2)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_FAILED);
            break;
        }
        Phone_ApplyClubLinkType();
        gFieldLinkPlayerCount = GetLinkPlayerCount_2();
        gLocalLinkPlayerId = GetMultiplayerId();
        SaveLinkPlayers(gFieldLinkPlayerCount);
        data[1] = 0;
        data[0] = CLUB_HANDOFF_SEND_CARD;
        break;
    case CLUB_HANDOFF_SEND_CARD:
        if (HasLinkErrorOccurred() == TRUE)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_CONNECTION_ERROR);
            break;
        }
        if (!IsLinkTaskFinished())
            break;
        TrainerCard_GenerateCardForLinkPlayer((struct TrainerCard *)gBlockSendBuffer);
        if (!SendBlockRequest(BLOCK_REQ_SIZE_100))
        {
            if (++data[1] > 240)
                Phone_FinishClubLinkup(taskId, LINKUP_CONNECTION_ERROR);
            break;
        }
        data[1] = 0;
        data[0] = CLUB_HANDOFF_WAIT_CARD;
        break;
    case CLUB_HANDOFF_WAIT_CARD:
        if (HasLinkErrorOccurred() == TRUE)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_CONNECTION_ERROR);
            break;
        }
        if (GetBlockReceivedStatus() != GetSavedLinkPlayerCountAsBitFlags())
        {
            if (++data[1] > 240)
                Phone_FinishClubLinkup(taskId, LINKUP_CONNECTION_ERROR);
            break;
        }
        for (i = 0; i < GetLinkPlayerCount(); i++)
            gTrainerCards[i] = *(const struct TrainerCard *)gBlockRecvBuffer[i];
        ResetBlockReceivedFlags();
        Phone_FinishClubLinkup(taskId, LINKUP_SUCCESS);
        break;
    }
}

static void VBlankCB_PhoneAgenda(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void ShowPhoneAgenda(MainCallback exitCallback)
{
    // A remote request can ask to open the Agenda while it is already open.
    // Allocating twice would leak the first arena and leave two owners of it.
    if (sAgenda != NULL)
        return;

    Phone_EnsureReady();
    sAgenda = AllocZeroed(sizeof(*sAgenda));
    if (sAgenda == NULL)
        return;
    sAgenda->savedCallback = exitCallback;
    sAgenda->loadState = 0;
    sAgenda->uiState = AGENDA_STATE_MAIN;
    sAgenda->listTaskId = TASK_NONE;
    sAgendaSeenLink = 0xFFFF;
    SetMainCallback2(CB2_PhoneAgenda);
}

// Worst case: 7 name + 1 pad + 1 space + 5 digits + 5 status + terminator.
// LABEL_WIDTH must stay comfortably above that.
static void Phone_FormatLabel(u32 destIndex, const struct PhoneContact *contact)
{
    u8 *label = sAgenda->labels[destIndex];
    u8 *ptr;
    u16 displayId;

    Phone_CopyPeerName(label, contact->name);
    ptr = label + StringLength(label);
    while (ptr - label < 8)
        *ptr++ = CHAR_SPACE;
    *ptr++ = CHAR_SPACE;
    displayId = Phone_GetDisplayId(contact->trainerId);
    ConvertIntToDecimalStringN(ptr, displayId, STR_CONV_MODE_LEADING_ZEROS, 5);
    StringAppend(label, Phone_IsTrainerOnline(contact->trainerId) ? sText_On : sText_Offline);
}

static void Phone_DestroyList(void)
{
    if (sAgenda->listTaskId != TASK_NONE)
    {
        DestroyListMenuTask(sAgenda->listTaskId, NULL, NULL);
        sAgenda->listTaskId = TASK_NONE;
    }
    sAgenda->numItems = 0;
}

static void Phone_BuildAgendaList(void)
{
    u32 i, n = 0;
    struct ListMenuTemplate template;

    Phone_DestroyList();
    FillWindowPixelBuffer(WIN_LIST, PIXEL_FILL(1));

    for (i = 0; i < PHONE_MAX_CONTACTS; i++)
    {
        if (gSaveBlock2Ptr->phone.contacts[i].trainerId != 0)
        {
            Phone_FormatLabel(n, &gSaveBlock2Ptr->phone.contacts[i]);
            sAgenda->items[n].label = sAgenda->labels[n];
            sAgenda->items[n].index = i;
            n++;
        }
    }
    sAgenda->numItems = n;

    if (n != 0)
    {
        template = sAgendaListTemplate;
        template.items = sAgenda->items;
        template.totalItems = n;
        if (template.maxShowed > n)
            template.maxShowed = n;
        sAgenda->listTaskId = ListMenuInit(&template, 0, 0);
    }
    else
    {
        AddTextPrinterParameterized3(WIN_LIST, FONT_NORMAL, 8, 1, sMenuTextColor, TEXT_SKIP_DRAW, sText_NoContacts);
        PutWindowTilemap(WIN_LIST);
        CopyWindowToVram(WIN_LIST, COPYWIN_FULL);
    }
}

static void Phone_PrintHeader(void)
{
    u16 id;

    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized3(WIN_HEADER, FONT_NORMAL, 8, 1, sMenuTextColor, TEXT_SKIP_DRAW, sText_AgendaTitle);

    StringCopy(gStringVar1, gSaveBlock2Ptr->playerName);
    StringAppend(gStringVar1, sText_IdLabel);
    id = Phone_GetPlayerDisplayId();
    ConvertIntToDecimalStringN(gStringVar2, id, STR_CONV_MODE_LEADING_ZEROS, 5);
    StringAppend(gStringVar1, gStringVar2);
    StringAppend(gStringVar1, FlagGet(FLAG_SYS_CONNECTOR_ON) ? sText_On : sText_Off);
    AddTextPrinterParameterized3(WIN_HEADER, FONT_SMALL, 8, 17, sMenuTextColor, TEXT_SKIP_DRAW, gStringVar1);

    PutWindowTilemap(WIN_HEADER);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
}

static void Phone_PrintFooter(const u8 *str)
{
    FillWindowPixelBuffer(WIN_FOOTER, PIXEL_FILL(1));
    AddTextPrinterParameterized3(WIN_FOOTER, FONT_SMALL, 8, 1, sMenuTextColor, TEXT_SKIP_DRAW, str);
    PutWindowTilemap(WIN_FOOTER);
    CopyWindowToVram(WIN_FOOTER, COPYWIN_FULL);
}

static void Phone_PrintDiagRow(u8 y, const u8 *label, const u16 *values, u8 count)
{
    u8 buf[8 * 6 + 1];
    u8 *ptr = buf;
    u8 i;

    for (i = 0; i < count && i < 8; i++)
    {
        if (i != 0)
            *ptr++ = CHAR_SLASH;
        ptr = ConvertIntToDecimalStringN(ptr, values[i], STR_CONV_MODE_LEFT_ALIGN, 5);
    }
    *ptr = EOS;

    AddTextPrinterParameterized3(WIN_LIST, FONT_SMALL, 4, y, sMenuTextColor, TEXT_SKIP_DRAW, label);
    AddTextPrinterParameterized3(WIN_LIST, FONT_SMALL, 116, y, sMenuTextColor, TEXT_SKIP_DRAW, buf);
}

// Renders into the existing list window, so entering diagnostics allocates
// nothing and touches no VRAM the Agenda did not already own.
static void Phone_ShowDiagnostics(void)
{
    const struct LinkDiagStats *stats = LinkDiag_GetStats();
    u16 row[4];

    Phone_DestroyList();
    FillWindowPixelBuffer(WIN_LIST, PIXEL_FILL(1));

    row[0] = stats->events[LINK_DIAG_SESSION_OPENED];
    row[1] = stats->events[LINK_DIAG_SESSION_ESTABLISHED];
    row[2] = stats->events[LINK_DIAG_SESSION_DROPPED];
    Phone_PrintDiagRow(1, sText_DiagSessions, row, 3);

    row[0] = stats->events[LINK_DIAG_PKT_SENT];
    row[1] = stats->events[LINK_DIAG_PKT_RECV];
    row[2] = stats->events[LINK_DIAG_PKT_SEND_FAILED];
    Phone_PrintDiagRow(13, sText_DiagPackets, row, 3);

    row[0] = stats->events[LINK_DIAG_PKT_BAD_MAGIC];
    row[1] = stats->events[LINK_DIAG_PKT_BAD_VERSION];
    row[2] = stats->events[LINK_DIAG_PKT_BAD_CHANNEL];
    Phone_PrintDiagRow(25, sText_DiagRejected, row, 3);

    row[0] = stats->hardwareErrors;
    row[1] = stats->checksumErrors;
    row[2] = stats->queueFullErrors;
    row[3] = stats->lagErrors;
    Phone_PrintDiagRow(37, sText_DiagErrors, row, 4);

    row[0] = stats->events[LINK_DIAG_TIMEOUT];
    row[1] = stats->events[LINK_DIAG_PEER_SANITIZED];
    row[2] = stats->peakRecvQueue;
    Phone_PrintDiagRow(49, sText_DiagMisc, row, 3);

    PutWindowTilemap(WIN_LIST);
    CopyWindowToVram(WIN_LIST, COPYWIN_FULL);
}

static void Phone_BeginRemoveConfirm(u32 contactSlot)
{
    sAgenda->selectedSlot = contactSlot;
    sAgenda->uiState = AGENDA_STATE_CONFIRM_REMOVE;
    Phone_PrintFooter(sText_FooterConfirm);
    CreateYesNoMenu(&sYesNoTemplate, FONT_NORMAL, 0, 2, 0x214, 14, 1);
}

static void Phone_RedrawAgendaWindows(void)
{
    PutWindowTilemap(WIN_HEADER);
    PutWindowTilemap(WIN_LIST);
    PutWindowTilemap(WIN_FOOTER);
    ClearWindowTilemap(WIN_ACTION);
    CopyBgTilemapBufferToVram(0);
}

static void Phone_DestroyActionWindow(void)
{
    if (sAgenda == NULL)
        return;
    ClearWindowTilemap(WIN_ACTION);
    Phone_RedrawAgendaWindows();
}

static void Phone_BeginActionMenu(u32 contactSlot)
{
    u8 lineHeight = GetFontAttribute(FONT_NORMAL, FONTATTR_MAX_LETTER_HEIGHT) + 2;

    sAgenda->selectedSlot = contactSlot;
    sAgenda->uiState = AGENDA_STATE_ACTIONS;
    Phone_PrintFooter(sText_FooterActions);
    FillWindowPixelBuffer(WIN_ACTION, PIXEL_FILL(1));
    SetStdWindowBorderStyle(WIN_ACTION, FALSE);
    AddItemMenuActionTextPrinters(
        WIN_ACTION,
        FONT_NORMAL,
        GetMenuCursorDimensionByFont(FONT_NORMAL, 0),
        2,
        GetFontAttribute(FONT_NORMAL, FONTATTR_LETTER_SPACING),
        lineHeight,
        PHONE_ACTION_COUNT,
        sPhoneActions,
        sPhoneActionOrder);
    Menu_InitCursor(WIN_ACTION, FONT_NORMAL, 0, 2, lineHeight, PHONE_ACTION_COUNT, 0);
    CopyWindowToVram(WIN_ACTION, COPYWIN_FULL);
}

static void Phone_QueueClubActivity(u8 taskId, u8 kind)
{
    Phone_DestroyActionWindow();
    sPhoneClubBusy = TRUE;
    sPendingPhoneActivity = kind;
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[taskId].func = Task_ClosePhoneAgenda;
}

static void Phone_HandleActionMenu(u8 taskId)
{
    s8 input = Menu_ProcessInputNoWrapAround();
    u8 kind;

    if (input == MENU_NOTHING_CHOSEN)
        return;

    if (input == MENU_B_PRESSED || input == PHONE_ACTION_CANCEL)
    {
        PlaySE(SE_SELECT);
        Phone_DestroyActionWindow();
        sAgenda->uiState = AGENDA_STATE_MAIN;
        Phone_PrintFooter(sText_FooterMain);
        return;
    }

    if (input == PHONE_ACTION_REMOVE)
    {
        PlaySE(SE_SELECT);
        Phone_DestroyActionWindow();
        Phone_BeginRemoveConfirm(sAgenda->selectedSlot);
        return;
    }

    PlaySE(SE_SELECT);
    if (input == PHONE_ACTION_TRADE)
    {
        if (!Phone_PartyCanTrade())
        {
            PlaySE(SE_BOO);
            Phone_DestroyActionWindow();
            sAgenda->uiState = AGENDA_STATE_DECLINED;
            Phone_PrintFooter(Phone_TradeBlockReason());
            return;
        }
        kind = PHONE_MSG_TRADE;
    }
    else
        kind = PHONE_MSG_BATTLE;
    if (!Phone_TrySendMsg(kind))
    {
        PlaySE(SE_BOO);
        return;
    }
    Phone_DestroyActionWindow();
    sOutgoingKind = kind;
    sWaitingReply = TRUE;
    sAgenda->uiState = AGENDA_STATE_WAIT_REPLY;
    Phone_PrintFooter(sText_FooterWaiting);
}

static void Phone_BeginIncomingPrompt(void)
{
    Phone_DestroyActionWindow();
    sAgenda->uiState = AGENDA_STATE_INCOMING;
    Phone_PrintFooter(sIncomingKind == PHONE_MSG_TRADE ? sText_IncomingTrade : sText_IncomingBattle);
    CreateYesNoMenu(&sYesNoTemplate, FONT_NORMAL, 0, 2, 0x214, 14, 1);
}

static void Phone_HandleIncomingConfirm(u8 taskId)
{
    s8 input = Menu_ProcessInputNoWrapAround();

    if (input == MENU_NOTHING_CHOSEN)
        return;

    if (input == 0)
    {
        PlaySE(SE_SELECT);
        if (sIncomingKind == PHONE_MSG_TRADE && !Phone_PartyCanTrade())
        {
            PlaySE(SE_BOO);
            DestroyYesNoMenu();
            sIncomingKind = 0;
            sAgenda->uiState = AGENDA_STATE_DECLINED;
            Phone_PrintFooter(Phone_TradeBlockReason());
            return;
        }
        if (!Phone_TrySendMsg(PHONE_MSG_ACCEPT))
        {
            PlaySE(SE_BOO);
            return;
        }
        DestroyYesNoMenu();
        sPendingPhoneActivity = (sIncomingKind == PHONE_MSG_TRADE) ? PHONE_PENDING_TRADE : PHONE_PENDING_BATTLE;
        sIncomingKind = 0;
        sFlushDelay = 0;
        sAgenda->uiState = AGENDA_STATE_FLUSH_ACCEPT;
    }
    else
    {
        PlaySE(SE_SELECT);
        if (!Phone_TrySendMsg(PHONE_MSG_DECLINE))
        {
            PlaySE(SE_BOO);
            return;
        }
        DestroyYesNoMenu();
        sIncomingKind = 0;
        sAgenda->uiState = AGENDA_STATE_MAIN;
        Phone_PrintFooter(sText_FooterMain);
        PutWindowTilemap(WIN_HEADER);
        PutWindowTilemap(WIN_LIST);
        PutWindowTilemap(WIN_FOOTER);
        CopyBgTilemapBufferToVram(0);
    }
}

static void Phone_HandleFlushAccept(u8 taskId)
{
    u16 linkType;

    if (sPendingPhoneActivity == PHONE_PENDING_TRADE)
    {
        gSpecialVar_0x8004 = USING_TRADE_CENTER;
        linkType = LINKTYPE_TRADE_SETUP;
    }
    else
    {
        gSpecialVar_0x8004 = USING_SINGLE_BATTLE;
        linkType = LINKTYPE_SINGLE_BATTLE;
    }

    if (sPendingPhoneActivity == PHONE_PENDING_TRADE && !Phone_PartyCanTrade())
    {
        LinkSession_CancelHandoff();
        sPendingPhoneActivity = PHONE_PENDING_NONE;
        sAgenda->uiState = AGENDA_STATE_DECLINED;
        Phone_PrintFooter(Phone_TradeBlockReason());
        return;
    }

    if (!LinkSession_IsHandoffPending() && !LinkSession_IsHandoffReady())
        LinkSession_RequestHandoff(linkType);

    if (!LinkSession_IsHandoffReady())
    {
        if (++sFlushDelay > 240)
        {
            sFlushDelay = 0;
            LinkSession_CancelHandoff();
            sPendingPhoneActivity = PHONE_PENDING_NONE;
            sAgenda->uiState = AGENDA_STATE_DECLINED;
            Phone_PrintFooter(sText_RequestDeclined);
        }
        return;
    }

    sFlushDelay = 0;
    LinkSession_BeginHandoff();
    Phone_QueueClubActivity(taskId, sPendingPhoneActivity);
}

static void Phone_HandleWaitReply(u8 taskId)
{
    if (sPendingPhoneActivity != PHONE_PENDING_NONE)
    {
        sFlushDelay = 0;
        sAgenda->uiState = AGENDA_STATE_FLUSH_ACCEPT;
        return;
    }
    if (sOutgoingKind != 0 && (!gReceivedRemoteLinkPlayers || !sPhoneLinkConnected))
    {
        sWaitingReply = FALSE;
        sOutgoingKind = 0;
        sAgenda->uiState = AGENDA_STATE_DECLINED;
        Phone_PrintFooter(sText_RequestDeclined);
        return;
    }
    if (sIncomingKind == PHONE_MSG_DECLINE)
    {
        sIncomingKind = 0;
        sAgenda->uiState = AGENDA_STATE_DECLINED;
        Phone_PrintFooter(sText_RequestDeclined);
        return;
    }
    if (sIncomingKind == PHONE_MSG_BATTLE || sIncomingKind == PHONE_MSG_TRADE)
    {
        Phone_BeginIncomingPrompt();
        return;
    }
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        Phone_TrySendMsg(PHONE_MSG_DECLINE);
        sWaitingReply = FALSE;
        sOutgoingKind = 0;
        sAgenda->uiState = AGENDA_STATE_MAIN;
        Phone_PrintFooter(sText_FooterMain);
    }
}

static void Phone_HandleRemoveConfirm(u8 taskId)
{
    s8 input = Menu_ProcessInputNoWrapClearOnChoose();

    if (input == MENU_NOTHING_CHOSEN)
        return;

    if (input == 0)
    {
        PlaySE(SE_SELECT);
        Phone_RemoveContact(sAgenda->selectedSlot);
        sAgenda->uiState = AGENDA_STATE_MAIN;
        Phone_PrintFooter(sText_FooterMain);
        Phone_BuildAgendaList();
        CopyBgTilemapBufferToVram(0);
    }
    else
    {
        PlaySE(SE_SELECT);
        sAgenda->uiState = AGENDA_STATE_MAIN;
        Phone_PrintFooter(sText_FooterMain);
        PutWindowTilemap(WIN_HEADER);
        PutWindowTilemap(WIN_LIST);
        PutWindowTilemap(WIN_FOOTER);
        CopyBgTilemapBufferToVram(0);
    }
}

static void CB2_PhoneAgenda(void)
{
    switch (sAgenda->loadState)
    {
    case 0:
        SetVBlankCallback(NULL);
        SetHBlankCallback(NULL);
        ScanlineEffect_Stop();
        ResetPaletteFade();
        // The session is not a task, so ResetTasks() no longer destroys it and
        // nothing has to be stashed across the screen transition.
        ResetTasks();
        ResetSpriteData();
        FreeAllSpritePalettes();
        DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
        DmaClear32(3, (void *)OAM, OAM_SIZE);
        DmaClear16(3, (void *)PLTT, PLTT_SIZE);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BLDCNT, 0);
        SetGpuReg(REG_OFFSET_BLDY, 0);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sAgendaBgTemplates, NELEMS(sAgendaBgTemplates));
        ResetBgPositions();
        InitWindows(sAgendaWinTemplates);
        DeactivateAllTextPrinters();
        LoadPalette(sBlackPal, 0, sizeof(sBlackPal));
        LoadStdWindowFrameGfx();
        sAgenda->loadState++;
        break;
    case 1:
        FillBgTilemapBufferRect_Palette0(0, 0, 0, 0, 32, 32);
        SetStdWindowBorderStyle(WIN_HEADER, FALSE);
        SetStdWindowBorderStyle(WIN_LIST, FALSE);
        SetStdWindowBorderStyle(WIN_FOOTER, FALSE);
        Phone_PrintHeader();
        Phone_PrintFooter(sText_FooterMain);
        Phone_BuildAgendaList();
        ClearWindowTilemap(WIN_ACTION);
        CopyBgTilemapBufferToVram(0);
        ShowBg(0);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON | DISPCNT_BG0_ON);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB_PhoneAgenda);
        CreateTask(Task_PhoneAgenda, 0);
        sAgenda->loadState++;
        break;
    default:
        // The Agenda owns the CPU while open, so it must keep the session
        // ticking or the link would stall behind this screen.
        LinkSession_Update();
        Phone_SyncOnlinePeers();
        LinkCoop_Update();
        RunTasks();
        AnimateSprites();
        BuildOamBuffer();
        DoScheduledBgTilemapCopiesToVram();
        UpdatePaletteFade();
        break;
    }
}

static void Phone_RefreshAgendaIfNeeded(void)
{
    u16 seen = sPhoneLinkConnected + (sPhoneOnlineCount << 1) + (Phone_CountContacts() << 4);

    if (seen == sAgendaSeenLink || sAgenda->uiState != AGENDA_STATE_MAIN)
        return;
    sAgendaSeenLink = seen;
    Phone_BuildAgendaList();
    CopyBgTilemapBufferToVram(0);
}

static void Task_PhoneAgenda(u8 taskId)
{
    s32 input;

    if (gPaletteFade.active)
        return;

    Phone_RefreshAgendaIfNeeded();
    Phone_PollLinkMsgs();

    if (LinkProto_HasVersionMismatch() && sAgenda->uiState == AGENDA_STATE_MAIN)
        Phone_PrintFooter(sText_RomMismatch);

    if (sPendingPhoneActivity != PHONE_PENDING_NONE
     && sAgenda->uiState != AGENDA_STATE_WAIT_REPLY
     && sAgenda->uiState != AGENDA_STATE_FLUSH_ACCEPT)
    {
        sFlushDelay = 0;
        sAgenda->uiState = AGENDA_STATE_FLUSH_ACCEPT;
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_MAIN
     && (sIncomingKind == PHONE_MSG_BATTLE || sIncomingKind == PHONE_MSG_TRADE))
    {
        Phone_BeginIncomingPrompt();
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_WAIT_REPLY)
    {
        Phone_HandleWaitReply(taskId);
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_INCOMING)
    {
        Phone_HandleIncomingConfirm(taskId);
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_FLUSH_ACCEPT)
    {
        Phone_HandleFlushAccept(taskId);
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_DECLINED)
    {
        if (JOY_NEW(A_BUTTON) || JOY_NEW(B_BUTTON))
        {
            sAgenda->uiState = AGENDA_STATE_MAIN;
            Phone_PrintFooter(sText_FooterMain);
        }
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_DIAG)
    {
        if (JOY_NEW(SELECT_BUTTON) || JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            sAgenda->uiState = AGENDA_STATE_MAIN;
            Phone_PrintFooter(sText_FooterMain);
            Phone_BuildAgendaList();
            CopyBgTilemapBufferToVram(0);
        }
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_CONFIRM_REMOVE)
    {
        Phone_HandleRemoveConfirm(taskId);
        return;
    }

    if (sAgenda->uiState == AGENDA_STATE_ACTIONS)
    {
        Phone_HandleActionMenu(taskId);
        return;
    }

    if (JOY_NEW(SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        sAgenda->uiState = AGENDA_STATE_DIAG;
        Phone_PrintFooter(sText_FooterDiag);
        Phone_ShowDiagnostics();
        CopyBgTilemapBufferToVram(0);
        return;
    }

    if (sAgenda->numItems != 0)
    {
        input = ListMenu_ProcessInput(sAgenda->listTaskId);
        if (input == LIST_CANCEL)
        {
            PlaySE(SE_SELECT);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            gTasks[taskId].func = Task_ClosePhoneAgenda;
        }
        else if (input != LIST_NOTHING_CHOSEN)
        {
            PlaySE(SE_SELECT);
            if (Phone_IsTrainerOnline(gSaveBlock2Ptr->phone.contacts[input].trainerId))
                Phone_BeginActionMenu(input);
            else
                Phone_BeginRemoveConfirm(input);
        }
    }
    else if (JOY_NEW(A_BUTTON) || JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_ClosePhoneAgenda;
    }
}

static void Task_ClosePhoneAgenda(u8 taskId)
{
    MainCallback cb;

    if (gPaletteFade.active)
        return;

    Phone_DestroyActionWindow();
    Phone_DestroyList();
    FreeAllWindowBuffers();
    cb = sAgenda->savedCallback;
    TRY_FREE_AND_SET_NULL(sAgenda);
    // CloseLink leaves CB1 as CB1_UpdateLinkState. CB2_ReturnToField then
    // takes the cable-club path (WIN0 black bar) and never runs DoCB1_Overworld,
    // so the Colosseum/Trade handshake never starts.
    SetMainCallback1(CB1_Overworld);
    if (sPendingPhoneActivity == PHONE_PENDING_TRADE)
    {
        sPendingPhoneActivity = PHONE_PENDING_NONE;
        Phone_EnterOverworldTrade();
    }
    else if (sPendingPhoneActivity != PHONE_PENDING_NONE)
    {
        ScriptContext_SetupScript(EventScript_PhoneTryBattle);
        sPendingPhoneActivity = PHONE_PENDING_NONE;
        gFieldCallback = Phone_FieldCB_ReturnForLinkup;
        SetMainCallback2(CB2_ReturnToField);
    }
    else
    {
        SetMainCallback2(cb);
    }
    DestroyTask(taskId);
}
