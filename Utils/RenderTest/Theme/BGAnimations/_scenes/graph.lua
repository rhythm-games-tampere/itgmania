-- GraphDisplay (life graph body + GraphLine) from the perf run's stage stats.
local ss = STATSMAN:GetCurStageStats()
local af = Def.ActorFrame{}
for i, pn in ipairs({ PLAYER_1, PLAYER_2 }) do
	af[#af+1] = Def.GraphDisplay{
		InitCommand=function(self)
			self:Load("GraphDisplay")
			self:Set(ss, ss:GetPlayerStageStats(pn))
			self:xy(_screen.cx, _screen.h * (i == 1 and 0.3 or 0.7)):zoom(4)
		end }
end
return af
