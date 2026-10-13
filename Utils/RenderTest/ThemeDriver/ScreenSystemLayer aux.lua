-- Drives a real theme for `rendertest.py run --theme DIR` (written for Simply
-- Love). rendertest.py adds this file to a copy of the theme's BGAnimations;
-- the system layer loads it on top of every screen. Each step optionally
-- jumps to a screen, waits until that screen is on top, counts frames and
-- takes a screenshot. Frame counts are deterministic because the harness sets
-- ConstantUpdateDeltaSeconds and FrameLockedSongTime.

local SONG = "StepMania 5/Springtime"
-- Give up on a step after this many frames (a screen that never shows up).
local STEP_TIMEOUT = 20000

local function JoinPlayers()
	GAMESTATE:Reset()
	GAMESTATE:SetCurrentPlayMode("PlayMode_Regular")
	GAMESTATE:JoinPlayer(PLAYER_1)
	GAMESTATE:JoinPlayer(PLAYER_2)
	GAMESTATE:SetCurrentStyle("versus")
	local song = SONGMAN:FindSong(SONG)
	assert(song, "RenderTest: song not found: " .. SONG)
	GAMESTATE:SetCurrentSong(song)
	local steps = song:GetOneSteps("StepsType_Dance_Single", "Difficulty_Challenge")
	for _, pn in ipairs({ PLAYER_1, PLAYER_2 }) do
		GAMESTATE:SetCurrentSteps(pn, steps)
	end
end

-- The whole song again with RenderProfiler on, like the RenderTest theme's
-- perf step. The profiler writes its report when gameplay ends.
local config = IniFile.ReadFile("/Save/RenderTest.ini").RenderTest or {}

local function StartPerf()
	JoinPlayers()
	PREFSMAN:SetPreference("RenderProfiler", config.ProfilerMode or 2)
	-- No screenshots in this run, so no back buffer copy every frame.
	PREFSMAN:SetPreference("ScreenshotBackBuffer", false)
end

local steps = {
	{ go = "ScreenTitleMenu", frames = 120, shot = "title" },
	{ go = "ScreenSelectMusic", setup = JoinPlayers, frames = 180, shot = "selectmusic" },
	{ go = "ScreenPlayerOptions", frames = 60, shot = "playeroptions" },
	{ go = "ScreenGameplay", frames = 1500, shot = "gameplay" },
	-- The theme moves on to evaluation by itself when the song ends.
	{ screen = "ScreenEvaluationStage", frames = 180, shot = "evaluation" },
}

-- Options from rendertest.py (config, above).
if config.SkipPerf ~= 1 then
	steps[#steps + 1] = { go = "ScreenGameplay", setup = StartPerf, frames = 1 }
	steps[#steps + 1] = { screen = "ScreenEvaluationStage", frames = 1 }
end

local index, frames, total = 0, 0, 0
local step

local function Next()
	index = index + 1
	step = steps[index]
	frames, total = 0, 0
	if not step then
		Trace("RenderTest: done")
		SCREENMAN:SetNewScreen("ScreenExit")
		return
	end
	step.screen = step.screen or step.go
	if step.setup then step.setup() end
	if step.go then SCREENMAN:SetNewScreen(step.go) end
end

return Def.ActorFrame{
	InitCommand=function(self)
		self:SetUpdateFunction(function()
			if index == 0 then Next() end
			if not step then return end
			total = total + 1
			if total > STEP_TIMEOUT then
				Trace("RenderTest: step " .. index .. " ok=false (never reached " .. step.screen .. ")")
				return Next()
			end
			local top = SCREENMAN:GetTopScreen()
			if not top or top:GetName() ~= step.screen then return end
			frames = frames + 1
			if frames == step.frames then
				if step.shot then
					local ok, path = SaveScreenshot(nil, false, false, step.shot .. "__", "")
					Trace("RenderTest: screenshot " .. step.shot .. " -> " .. tostring(path) .. " ok=" .. tostring(ok))
				end
				Next()
			end
		end)
	end,
}
