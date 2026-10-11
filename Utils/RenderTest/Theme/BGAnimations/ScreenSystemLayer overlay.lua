-- _fallback's system layer without the system message box: messages such as
-- "Connected: <device>" (CI machines' virtual input devices) would otherwise
-- land in screenshots. The aux layer and credit texts stay as they were.
local function CreditsText(pn)
	return LoadFont(Var "LoadingScreen", "credits") .. {
		InitCommand=function(self)
			self:name("Credits" .. PlayerNumberToString(pn))
			ActorUtil.LoadAllCommandsAndSetXY(self, Var "LoadingScreen")
		end,
		UpdateTextCommand=function(self)
			self:settext(ScreenSystemLayerHelpers.GetCreditsMessage(pn))
		end,
		UpdateVisibleCommand=function(self)
			local screen = SCREENMAN:GetTopScreen()
			self:visible(not screen or THEME:GetMetric(screen:GetName(), "ShowCreditDisplay"))
		end,
	}
end

return Def.ActorFrame {
	LoadActor(THEME:GetPathB("ScreenSystemLayer", "aux")),
	Def.ActorFrame {
		CreditsText(PLAYER_1),
		CreditsText(PLAYER_2),
	},
}
