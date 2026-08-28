#include "global.h"
#include "gflib.h"
#include "characters.h"
#include "phone.h"
#include "battle.h"
#include "event_data.h"
#include "item.h"
#include "item_menu.h"
#include "link.h"
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
#include "trainer_card.h"
#include "window.h"
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
};

enum {
    PHONE_MSG_NONE = 0,
    PHONE_MSG_BATTLE = 1,
    PHONE_MSG_TRADE = 2,
    PHONE_MSG_ACCEPT = 3,
    PHONE_MSG_DECLINE = 4,
};

#define PHONE_MSG_MAGIC 0xA7E1

struct PhoneLinkMsg
{
    u16 magic;
    u8 kind;
    u8 pad;
    u32 trainerId;
};

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

struct PhoneAgenda
{
    MainCallback savedCallback;
    u8 loadState;
    u8 uiState;
    u8 listTaskId;
    u8 numItems;
    u8 selectedSlot;
};

static EWRAM_DATA struct PhoneAgenda *sAgenda = NULL;
static EWRAM_DATA struct ListMenuItem *sAgendaItems = NULL;
static EWRAM_DATA u8 sAgendaLabels[PHONE_MAX_CONTACTS][LABEL_WIDTH] = {0};
static EWRAM_DATA u32 sPhoneOnlineIds[MAX_LINK_PLAYERS] = {0};
static EWRAM_DATA u8 sPhoneOnlineCount = 0;
static EWRAM_DATA bool8 sPhoneLinkConnected = FALSE;
static EWRAM_DATA u16 sAgendaSeenLink = 0xFFFF;
static EWRAM_DATA s16 sStashedPhoneTask[4] = {0};
static EWRAM_DATA bool8 sStashedPhoneTaskActive = FALSE;
static EWRAM_DATA u8 sPendingPhoneActivity = 0;
static EWRAM_DATA bool8 sPhoneClubBusy = FALSE;
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
static void Phone_ShutdownDiscovery(void);
static void Phone_TryClearStaleClubState(void);
static void Phone_FieldCB_ReturnForLinkup(void);
static void Task_PhoneLink(u8 taskId);
static void Task_PhoneClubLinkup(u8 taskId);
static void Phone_ClearOnlinePeers(void);
static void Phone_SyncOnlinePeers(void);
static bool8 Phone_ShouldKeepLink(void);
static bool8 Phone_CanStartCable(void);
static void Phone_StashLinkTask(void);
static void Phone_RestoreLinkTask(void);
static void Phone_RefreshAgendaIfNeeded(void);
static u32 Phone_GetPlayerTrainerId(void);
static bool8 Phone_TrySendMsg(u8 kind);
static void Phone_PollLinkMsgs(void);
static void Phone_BeginIncomingPrompt(void);
static void Phone_HandleIncomingConfirm(u8 taskId);
static void Phone_HandleFlushAccept(u8 taskId);
static void Phone_HandleWaitReply(u8 taskId);
static void Task_OpenAgendaIncoming(u8 taskId);
static void Phone_TryOpenAgendaForIncoming(void);

static const u8 sText_AgendaTitle[] = _("TRAINER AGENDA");
static const u8 sText_FooterMain[] = _("A: ACTION  B: EXIT");
static const u8 sText_FooterConfirm[] = _("Remove this contact?");
static const u8 sText_FooterActions[] = _("A: CHOOSE  B: BACK");
static const u8 sText_FooterWaiting[] = _("WAITING  B: CANCEL");
static const u8 sText_IncomingBattle[] = _("Battle request. Accept?");
static const u8 sText_IncomingTrade[] = _("Trade request. Accept?");
static const u8 sText_RequestDeclined[] = _("The other TRAINER declined.");
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
    if (gSaveBlock2Ptr->phone.magic != PHONE_SAVE_MAGIC)
        Phone_InitSave();
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
            StringCopy(gSaveBlock2Ptr->phone.contacts[i].name, name);
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

static bool8 Phone_ShouldKeepLink(void)
{
    if (sPendingPhoneActivity != PHONE_PENDING_NONE)
        return FALSE;
    if (sPhoneClubBusy)
        return FALSE;
    if (VarGet(VAR_CABLE_CLUB_STATE) != 0)
        return FALSE;
    if (!FlagGet(FLAG_SYS_CONNECTOR_ON))
        return FALSE;
    if (gMain.inBattle)
        return FALSE;
    if (InUnionRoom() == TRUE)
        return FALSE;
    if (GetSafariZoneFlag() == TRUE)
        return FALSE;
    return TRUE;
}

static bool8 Phone_CanStartCable(void)
{
    if (gPaletteFade.active)
        return FALSE;
    // Never OpenLink from Bag or Agenda — serial deadlock with the other mGBA.
    if (!Overworld_IsFieldCB2Active())
        return FALSE;
    return !ArePlayerFieldControlsLocked();
}

static void Phone_StashLinkTask(void)
{
    u8 id = FindTaskIdByFunc(Task_PhoneLink);

    if (id != TASK_NONE)
    {
        memcpy(sStashedPhoneTask, gTasks[id].data, sizeof(sStashedPhoneTask));
        sStashedPhoneTaskActive = TRUE;
    }
    else
    {
        sStashedPhoneTaskActive = FALSE;
    }
}

static void Phone_RestoreLinkTask(void)
{
    u8 id;

    if (!Phone_ShouldKeepLink())
    {
        Phone_ClearOnlinePeers();
        sStashedPhoneTaskActive = FALSE;
        return;
    }
    id = CreateTask(Task_PhoneLink, 80);
    if (sStashedPhoneTaskActive)
        memcpy(gTasks[id].data, sStashedPhoneTask, sizeof(sStashedPhoneTask));
    sStashedPhoneTaskActive = FALSE;
    if (gReceivedRemoteLinkPlayers && gLinkType == LINKTYPE_PHONE)
        Phone_SyncOnlinePeers();
}

static void Phone_SyncOnlinePeers(void)
{
    u8 i;
    u8 selfId = GetMultiplayerId();
    u8 count = GetLinkPlayerCount();

    Phone_ClearOnlinePeers();
    if (count < 2)
        return;

    for (i = 0; i < count && i < MAX_LINK_PLAYERS; i++)
    {
        if (i == selfId)
            continue;
        sPhoneOnlineIds[sPhoneOnlineCount++] = gLinkPlayers[i].trainerId;
        Phone_AddContact(gLinkPlayers[i].name, gLinkPlayers[i].trainerId, gLinkPlayers[i].gender, 0);
    }
    sPhoneLinkConnected = (sPhoneOnlineCount != 0);
}

void Phone_UpdateConnector(void)
{
    u8 id;

    if (FlagGet(FLAG_SYS_CONNECTOR_ON))
    {
        if (!FuncIsActiveTask(Task_PhoneLink))
            CreateTask(Task_PhoneLink, 80);
        return;
    }

    Phone_ClearOnlinePeers();
    sStashedPhoneTaskActive = FALSE;
    SetSuppressLinkErrorMessage(TRUE);
    CloseLink();
    id = FindTaskIdByFunc(Task_PhoneLink);
    if (id != TASK_NONE)
        DestroyTask(id);
}

void Phone_OnClubLinkupEnd(void)
{
    sPhoneClubBusy = FALSE;
    sPendingPhoneActivity = PHONE_PENDING_NONE;
    sOutgoingKind = 0;
    sIncomingKind = 0;
    sWaitingReply = FALSE;
    VarSet(VAR_CABLE_CLUB_STATE, 0);
    SetMainCallback1(CB1_Overworld);
    HelpSystem_Enable();
}

bool8 Phone_IsClubSessionActive(void)
{
    return sPhoneClubBusy;
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
    gSpecialVar_Result = (sPhoneClubBusy && !gReceivedRemoteLinkPlayers);
}

void Phone_WarpToReturnPoint(void)
{
    SetWarpDestinationToDynamicWarp(0);
    DoWarp();
}

static void Phone_ShutdownDiscovery(void)
{
    u8 id;

    sStashedPhoneTaskActive = FALSE;
    Phone_ClearOnlinePeers();
    SetSuppressLinkErrorMessage(TRUE);
    CloseLink();
    id = FindTaskIdByFunc(Task_PhoneLink);
    if (id != TASK_NONE)
        DestroyTask(id);
}

static void Phone_TryClearStaleClubState(void)
{
    if (VarGet(VAR_CABLE_CLUB_STATE) == 0 && !sPhoneClubBusy)
        return;
    if (gReceivedRemoteLinkPlayers)
        return;
    if (!Overworld_IsFieldCB2Active())
        return;
    if (gPaletteFade.active)
        return;
    if (ArePlayerFieldControlsLocked())
        return;
    switch (gMapHeader.mapType)
    {
    case MAP_TYPE_TOWN:
    case MAP_TYPE_CITY:
    case MAP_TYPE_ROUTE:
        SetSuppressLinkErrorMessage(TRUE);
        CloseLink();
        Phone_OnClubLinkupEnd();
        break;
    }
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
        ScriptContext_SetupScript(EventScript_PhoneTryTrade);
    else
        ScriptContext_SetupScript(EventScript_PhoneTryBattle);
    sPendingPhoneActivity = PHONE_PENDING_NONE;
}

static u32 Phone_GetPlayerTrainerId(void)
{
    return T1_READ_32(gSaveBlock2Ptr->playerTrainerId);
}

static bool8 Phone_TrySendMsg(u8 kind)
{
    struct PhoneLinkMsg msg;

    if (!sPhoneLinkConnected)
        return FALSE;
    if (!IsLinkTaskFinished())
        return FALSE;

    msg.magic = PHONE_MSG_MAGIC;
    msg.kind = kind;
    msg.pad = 0;
    msg.trainerId = Phone_GetPlayerTrainerId();
    return SendBlock(0, &msg, sizeof(msg));
}

static void Phone_PollLinkMsgs(void)
{
    u8 status = GetBlockReceivedStatus();
    u8 i;
    u8 selfId = GetMultiplayerId();
    const struct PhoneLinkMsg *msg;

    if (status == 0)
        return;

    for (i = 0; i < MAX_LINK_PLAYERS; i++)
    {
        if (i == selfId || !((status >> i) & 1))
            continue;
        msg = (const struct PhoneLinkMsg *)gBlockRecvBuffer[i];
        if (msg->magic != PHONE_MSG_MAGIC)
        {
            ResetBlockReceivedFlag(i);
            continue;
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
        ResetBlockReceivedFlag(i);
    }
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

void Phone_TryResumeLink(void)
{
    Phone_TryClearStaleClubState();
    Phone_TryOpenAgendaForIncoming();
    if (Phone_ShouldKeepLink() && !FuncIsActiveTask(Task_PhoneLink))
        CreateTask(Task_PhoneLink, 80);
}

static void Phone_FinishClubLinkup(u8 taskId, u16 result)
{
    gSpecialVar_Result = result;
    SetSuppressLinkErrorMessage(TRUE);
    if (result != LINKUP_SUCCESS)
    {
        CloseLink();
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

static bool8 Phone_IsClubLinkType(u16 linkType)
{
    return linkType == LINKTYPE_SINGLE_BATTLE || linkType == LINKTYPE_TRADE_SETUP;
}

static bool8 Phone_ClubTypesMatch(u16 expected)
{
    u8 i;
    u8 n = GetLinkPlayerCount();

    if (n < 2)
        return FALSE;
    for (i = 0; i < n; i++)
    {
        if (gLinkPlayers[i].linkType != expected)
            return FALSE;
    }
    return TRUE;
}

static bool8 Phone_ClubSelectionsDiffer(void)
{
    u8 i;
    u8 n = GetLinkPlayerCount();

    if (n < 2)
        return FALSE;
    for (i = 0; i < n; i++)
    {
        if (!Phone_IsClubLinkType(gLinkPlayers[i].linkType))
            return FALSE;
    }
    for (i = 1; i < n; i++)
    {
        if (gLinkPlayers[i].linkType != gLinkPlayers[0].linkType)
            return TRUE;
    }
    return FALSE;
}

void TryPhoneClubLinkup(void)
{
    gSpecialVar_Result = LINKUP_ONGOING;
    SetSuppressLinkErrorMessage(TRUE);
    gLinkType = Phone_GetClubLinkType();
    if (gLinkType == LINKTYPE_TRADE_SETUP)
        gBattleTypeFlags = 0;
    CreateTask(Task_PhoneClubLinkup, 80);
}

enum {
    CLUB_LINK_WAIT_SI,
    CLUB_LINK_OPEN,
    CLUB_LINK_BOOT,
    CLUB_LINK_WAIT_PARTNER,
    CLUB_LINK_WAIT_EXCHANGE,
    CLUB_LINK_WAIT_CARD,
};

static void Task_PhoneClubLinkup(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 status;
    u8 playerCount;
    u8 i;

    switch (data[0])
    {
    case CLUB_LINK_WAIT_SI:
        SetSuppressLinkErrorMessage(TRUE);
        CloseLink();
        if (gReceivedRemoteLinkPlayers)
            break;
        if (++data[1] > 32)
        {
            data[1] = 0;
            data[2] = 0;
            data[0] = CLUB_LINK_OPEN;
        }
        break;
    case CLUB_LINK_OPEN:
        gWirelessCommType = 0;
        gLinkType = Phone_GetClubLinkType();
        OpenLinkTimed();
        SetSuppressLinkErrorMessage(TRUE);
        ResetLinkPlayers();
        ResetLinkPlayerCount();
        data[1] = 0;
        data[0] = CLUB_LINK_BOOT;
        break;
    case CLUB_LINK_BOOT:
        if (++data[1] > 9)
        {
            data[1] = 0;
            data[0] = CLUB_LINK_WAIT_PARTNER;
        }
        break;
    case CLUB_LINK_WAIT_PARTNER:
        playerCount = GetLinkPlayerCount_2();
        if (HasLinkErrorOccurred() == TRUE)
        {
            CloseLink();
            data[1] = 0;
            data[2] = 0;
            data[0] = CLUB_LINK_WAIT_SI;
            break;
        }
        if (Phone_ClubSelectionsDiffer())
        {
            Phone_FinishClubLinkup(taskId, LINKUP_DIFF_SELECTIONS);
            break;
        }
        if (playerCount >= 2)
        {
            if (!Phone_ClubTypesMatch(Phone_GetClubLinkType()))
            {
                CloseLink();
                data[1] = 0;
                data[2] = 0;
                data[0] = CLUB_LINK_WAIT_SI;
                break;
            }
            if (IsLinkMaster() == TRUE && data[2] == 0)
            {
                CheckShouldAdvanceLinkState();
                data[2] = 1;
            }
            data[1] = 0;
            data[0] = CLUB_LINK_WAIT_EXCHANGE;
        }
        else if (++data[1] > 480)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_FAILED);
        }
        break;
    case CLUB_LINK_WAIT_EXCHANGE:
        status = GetLinkPlayerDataExchangeStatusTimed(2, 2);
        if (status == EXCHANGE_COMPLETE)
        {
            gSpecialVar_Result = LINKUP_SUCCESS;
            gFieldLinkPlayerCount = GetLinkPlayerCount_2();
            gLocalLinkPlayerId = GetMultiplayerId();
            SaveLinkPlayers(gFieldLinkPlayerCount);
            TrainerCard_GenerateCardForLinkPlayer((struct TrainerCard *)gBlockSendBuffer);
            SendBlockRequest(BLOCK_REQ_SIZE_100);
            data[1] = 0;
            data[0] = CLUB_LINK_WAIT_CARD;
        }
        else if (status == EXCHANGE_DIFF_SELECTIONS)
        {
            if (Phone_ClubSelectionsDiffer())
            {
                Phone_FinishClubLinkup(taskId, LINKUP_DIFF_SELECTIONS);
            }
            else
            {
                CloseLink();
                data[1] = 0;
                data[2] = 0;
                data[0] = CLUB_LINK_WAIT_SI;
            }
        }
        else if (status == EXCHANGE_PLAYER_NOT_READY || status == EXCHANGE_PARTNER_NOT_READY)
        {
            if (++data[1] > 480)
                Phone_FinishClubLinkup(taskId, LINKUP_FAILED);
            break;
        }
        else if (HasLinkErrorOccurred() == TRUE)
        {
            CloseLink();
            data[1] = 0;
            data[2] = 0;
            data[0] = CLUB_LINK_WAIT_SI;
        }
        else if (++data[1] > 480)
        {
            Phone_FinishClubLinkup(taskId, LINKUP_FAILED);
        }
        break;
    case CLUB_LINK_WAIT_CARD:
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

#define tState data[0]
#define tTimer data[1]
#define tAdvanced data[2]
#define tChime data[3]

enum {
    PHONE_LINK_OPEN,
    PHONE_LINK_BOOT,
    PHONE_LINK_WAIT_PARTNER,
    PHONE_LINK_WAIT_EXCHANGE,
    PHONE_LINK_CONNECTED,
    PHONE_LINK_CLOSE,
};

static void Task_PhoneLink(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 status;
    u8 playerCount;

    if (!Phone_ShouldKeepLink() && tState != PHONE_LINK_CLOSE)
    {
        tState = PHONE_LINK_CLOSE;
        tTimer = 0;
    }

    switch (tState)
    {
    case PHONE_LINK_OPEN:
        // Bag must not OpenLink (stalls the item message on player 2).
        // Field and Agenda are OK.
        if (!Phone_CanStartCable())
            break;
        if (gReceivedRemoteLinkPlayers && gLinkType == LINKTYPE_PHONE)
        {
            Phone_SyncOnlinePeers();
            tState = PHONE_LINK_CONNECTED;
            break;
        }
        if (gReceivedRemoteLinkPlayers && gLinkType != LINKTYPE_PHONE)
        {
            DestroyTask(taskId);
            return;
        }
        gWirelessCommType = 0;
        gLinkType = LINKTYPE_PHONE;
        OpenLinkTimed();
        SetSuppressLinkErrorMessage(TRUE);
        ResetLinkPlayers();
        ResetLinkPlayerCount();
        tTimer = 0;
        tAdvanced = 0;
        tState = PHONE_LINK_BOOT;
        break;
    case PHONE_LINK_BOOT:
        if (++tTimer > 9)
        {
            tTimer = 0;
            tState = PHONE_LINK_WAIT_PARTNER;
        }
        break;
    case PHONE_LINK_WAIT_PARTNER:
        playerCount = GetLinkPlayerCount_2();
        if (HasLinkErrorOccurred() == TRUE)
        {
            CloseLink();
            tTimer = 0;
            tState = PHONE_LINK_OPEN;
            break;
        }
        if (playerCount >= 2)
        {
            if (IsLinkMaster() == TRUE && tAdvanced == 0)
            {
                CheckShouldAdvanceLinkState();
                tAdvanced = 1;
            }
            tTimer = 0;
            tState = PHONE_LINK_WAIT_EXCHANGE;
        }
        else if (++tTimer > 360)
        {
            CloseLink();
            tTimer = 0;
            tState = PHONE_LINK_OPEN;
        }
        break;
    case PHONE_LINK_WAIT_EXCHANGE:
        status = GetLinkPlayerDataExchangeStatusTimed(2, 4);
        if (status == EXCHANGE_COMPLETE)
        {
            Phone_SyncOnlinePeers();
            if (sPhoneLinkConnected)
                tChime = 1;
            tTimer = 0;
            tState = PHONE_LINK_CONNECTED;
        }
        else if (status == EXCHANGE_TIMED_OUT
              || status == EXCHANGE_WRONG_NUM_PLAYERS
              || status == EXCHANGE_DIFF_SELECTIONS
              || HasLinkErrorOccurred() == TRUE)
        {
            Phone_ClearOnlinePeers();
            CloseLink();
            tTimer = 0;
            tAdvanced = 0;
            tChime = 0;
            tState = PHONE_LINK_OPEN;
        }
        break;
    case PHONE_LINK_CONNECTED:
        playerCount = GetLinkPlayerCount_2();
        if (HasLinkErrorOccurred() == TRUE || playerCount < 2 || !gReceivedRemoteLinkPlayers)
        {
            if (++tTimer > 45)
            {
                Phone_ClearOnlinePeers();
                CloseLink();
                tTimer = 0;
                tAdvanced = 0;
                tChime = 0;
                tState = PHONE_LINK_OPEN;
            }
        }
        else
        {
            tTimer = 0;
            Phone_SyncOnlinePeers();
            Phone_PollLinkMsgs();
        }
        break;
    case PHONE_LINK_CLOSE:
        Phone_ClearOnlinePeers();
        SetSuppressLinkErrorMessage(TRUE);
        CloseLink();
        DestroyTask(taskId);
        break;
    }
}

#undef tState
#undef tTimer
#undef tAdvanced
#undef tChime

static void VBlankCB_PhoneAgenda(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void ShowPhoneAgenda(MainCallback exitCallback)
{
    Phone_EnsureReady();
    sAgenda = AllocZeroed(sizeof(*sAgenda));
    sAgenda->savedCallback = exitCallback;
    sAgenda->loadState = 0;
    sAgenda->uiState = AGENDA_STATE_MAIN;
    sAgenda->listTaskId = TASK_NONE;
    sAgendaSeenLink = 0xFFFF;
    SetMainCallback2(CB2_PhoneAgenda);
}

static void Phone_FormatLabel(u32 destIndex, const struct PhoneContact *contact)
{
    u8 *ptr;
    u16 displayId;

    ptr = StringCopy(sAgendaLabels[destIndex], contact->name);
    while (ptr - sAgendaLabels[destIndex] < 8)
        *ptr++ = CHAR_SPACE;
    *ptr++ = CHAR_SPACE;
    displayId = Phone_GetDisplayId(contact->trainerId);
    ConvertIntToDecimalStringN(ptr, displayId, STR_CONV_MODE_LEADING_ZEROS, 5);
    StringAppend(sAgendaLabels[destIndex],
                 Phone_IsTrainerOnline(contact->trainerId) ? sText_On : sText_Offline);
}

static void Phone_DestroyList(void)
{
    if (sAgenda->listTaskId != TASK_NONE)
    {
        DestroyListMenuTask(sAgenda->listTaskId, NULL, NULL);
        sAgenda->listTaskId = TASK_NONE;
    }
    TRY_FREE_AND_SET_NULL(sAgendaItems);
    sAgenda->numItems = 0;
}

static void Phone_BuildAgendaList(void)
{
    u32 i, n = 0;
    struct ListMenuTemplate template;

    Phone_DestroyList();
    FillWindowPixelBuffer(WIN_LIST, PIXEL_FILL(1));

    sAgendaItems = AllocZeroed(PHONE_MAX_CONTACTS * sizeof(struct ListMenuItem));
    for (i = 0; i < PHONE_MAX_CONTACTS; i++)
    {
        if (gSaveBlock2Ptr->phone.contacts[i].trainerId != 0)
        {
            Phone_FormatLabel(n, &gSaveBlock2Ptr->phone.contacts[i]);
            sAgendaItems[n].label = sAgendaLabels[n];
            sAgendaItems[n].index = i;
            n++;
        }
    }
    sAgenda->numItems = n;

    if (n != 0)
    {
        template = sAgendaListTemplate;
        template.items = sAgendaItems;
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
    Phone_ShutdownDiscovery();
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
        kind = PHONE_MSG_TRADE;
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
    s8 input = Menu_ProcessInputNoWrapClearOnChoose();

    if (input == MENU_NOTHING_CHOSEN)
        return;

    if (input == 0)
    {
        PlaySE(SE_SELECT);
        if (!Phone_TrySendMsg(PHONE_MSG_ACCEPT))
        {
            PlaySE(SE_BOO);
            return;
        }
        sPendingPhoneActivity = (sIncomingKind == PHONE_MSG_TRADE) ? PHONE_PENDING_TRADE : PHONE_PENDING_BATTLE;
        sIncomingKind = 0;
        sFlushDelay = 0;
        sAgenda->uiState = AGENDA_STATE_FLUSH_ACCEPT;
    }
    else
    {
        PlaySE(SE_SELECT);
        Phone_TrySendMsg(PHONE_MSG_DECLINE);
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
    if (gReceivedRemoteLinkPlayers && !IsLinkTaskFinished())
        return;
    if (gReceivedRemoteLinkPlayers && sFlushDelay < 24)
    {
        sFlushDelay++;
        return;
    }
    sFlushDelay = 0;
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
        sPendingPhoneActivity = (sOutgoingKind == PHONE_MSG_TRADE) ? PHONE_PENDING_TRADE : PHONE_PENDING_BATTLE;
        sFlushDelay = 0;
        sAgenda->uiState = AGENDA_STATE_FLUSH_ACCEPT;
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
        Phone_StashLinkTask();
        ResetTasks();
        Phone_RestoreLinkTask();
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

    if (sPendingPhoneActivity != PHONE_PENDING_NONE
     && sAgenda->uiState != AGENDA_STATE_WAIT_REPLY
     && sAgenda->uiState != AGENDA_STATE_FLUSH_ACCEPT)
    {
        Phone_QueueClubActivity(taskId, sPendingPhoneActivity);
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
    if (sPendingPhoneActivity != PHONE_PENDING_NONE)
    {
        if (sPendingPhoneActivity == PHONE_PENDING_TRADE)
            ScriptContext_SetupScript(EventScript_PhoneTryTrade);
        else
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
