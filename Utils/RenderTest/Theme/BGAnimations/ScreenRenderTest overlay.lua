-- Runs the next RenderTest step (see Scripts/RenderTest.lua).
local step = RenderTest.Next()

if not step then
	return Def.Actor{
		OnCommand=function()
			Trace("RenderTest: done")
			SCREENMAN:SetNewScreen("ScreenExit")
		end,
	}
end

if step.gameplay then
	return Def.Actor{
		OnCommand=function()
			RenderTest.SetupGameplay(step)
			SCREENMAN:SetNewScreen("ScreenGameplay")
		end,
	}
end

-- Scenes are static, so a few frames are enough for textures to settle.
-- With step.wait, first wait that many real seconds (for the movie decoder),
-- then broadcast RenderTestStart and start counting frames.
local SHOT_FRAME = 10
local frames = 0
local start = GetTimeSinceStart()
return Def.ActorFrame{
	InitCommand=function(self)
		self:SetUpdateFunction(function()
			if step.wait then
				if GetTimeSinceStart() - start < step.wait then return end
				step.wait = nil
				MESSAGEMAN:Broadcast("RenderTestStart")
			end
			frames = frames + 1
			if frames == SHOT_FRAME then
				RenderTest.Shot(step.name)
			elseif frames == SHOT_FRAME + 1 then
				SCREENMAN:SetNewScreen("ScreenRenderTest")
			end
		end)
	end,
	Def.Quad{ InitCommand=function(self) self:FullScreen():diffuse(color("0.18,0.2,0.24,1")) end },
	LoadActor("_scenes/" .. step.scene),
}
