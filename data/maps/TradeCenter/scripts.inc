TradeCenter_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, TradeCenter_OnFrame
	.byte 0

TradeCenter_OnFrame::
	map_script_2 VAR_TEMP_1, 0, TradeCenter_EventScript_TryPhoneLink
	.2byte 0

TradeCenter_EventScript_TryPhoneLink::
	setvar VAR_TEMP_1, 1
	special Phone_ShouldStartRoomLinkup
	goto_if_eq VAR_RESULT, FALSE, TradeCenter_EventScript_SkipPhoneLink
	setvar VAR_0x8004, USING_TRADE_CENTER
	special TryPhoneClubLinkup
	end

TradeCenter_EventScript_SkipPhoneLink::
	end
