#ifndef GUARD_PHONE_H
#define GUARD_PHONE_H

#include "global.h"
#include "main.h"

void Phone_InitSave(void);
void Phone_EnsureReady(void);
s32 Phone_FindByTrainerId(u32 trainerId);
bool8 Phone_AddContact(const u8 *name, u32 trainerId, u8 gender, u8 flags);
void Phone_RemoveContact(u32 index);
u32 Phone_CountContacts(void);
u16 Phone_GetDisplayId(u32 trainerId);
u16 Phone_GetPlayerDisplayId(void);
const struct PhoneContact *Phone_GetContact(u32 index);
void ShowPhoneAgenda(MainCallback exitCallback);
void GivePhoneKeyItems(void);
void Phone_UpdateConnector(void);
void Phone_TryResumeLink(void);
void Phone_TryStartPendingLinkup(void);
void Phone_OnClubLinkupEnd(void);
bool8 Phone_IsClubSessionActive(void);
void TryPhoneClubLinkup(void);
void Phone_SaveReturnWarp(void);
void Phone_ShouldStartRoomLinkup(void);
void Phone_WarpToReturnPoint(void);
bool8 Phone_IsTrainerOnline(u32 trainerId);
bool8 Phone_IsLinkConnected(void);

#endif // GUARD_PHONE_H
