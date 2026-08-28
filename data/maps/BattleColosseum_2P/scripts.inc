BattleColosseum_2P_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, BattleColosseum_2P_OnFrame
	.byte 0

BattleColosseum_2P_OnFrame::
	map_script_2 VAR_TEMP_1, 0, BattleColosseum_2P_EventScript_TryPhoneLink
	.2byte 0

BattleColosseum_2P_EventScript_TryPhoneLink::
	setvar VAR_TEMP_1, 1
	special Phone_ShouldStartRoomLinkup
	goto_if_eq VAR_RESULT, FALSE, BattleColosseum_2P_EventScript_SkipPhoneLink
	special HealPlayerParty
	special SavePlayerParty
	special LoadPlayerBag
	setvar VAR_0x8004, USING_SINGLE_BATTLE
	special TryPhoneClubLinkup
	end

BattleColosseum_2P_EventScript_SkipPhoneLink::
	end
