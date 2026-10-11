-- Screenshots for RenderTest gameplay steps. Steps without shots play the
-- whole song and return through ScreenGameplay's NextScreen.
local step = RenderTest.Current()
if not step or not step.shots then return Def.ActorFrame{} end

local last = 0
for frame in pairs(step.shots) do last = math.max(last, frame) end

local frames = 0
return Def.ActorFrame{
	InitCommand=function(self)
		self:SetUpdateFunction(function()
			frames = frames + 1
			local shot = step.shots[frames]
			if shot then
				RenderTest.Shot(step.name .. "-" .. shot)
			elseif frames == last + 1 then
				SCREENMAN:SetNewScreen("ScreenRenderTest")
			end
		end)
	end,
}
