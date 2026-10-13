-- Step list for Utils/RenderTest/rendertest.py. ScreenRenderTest runs the
-- steps in order: a scene step loads BGAnimations/_scenes/<scene>.lua and
-- takes one screenshot; a gameplay step plays the song on autoplay and takes
-- a screenshot at each listed frame. Frame counts are deterministic because
-- the harness sets ConstantUpdateDeltaSeconds and FrameLockedSongTime.
-- Screenshots are saved as Screenshots/<name>__<date>.png.

local SONG = "StepMania 5/Springtime"

RenderTest = {
	index = 0,
	steps = {
		{ name = "blend", scene = "blend" },
		{ name = "effectmodes", scene = "effectmodes" },
		{ name = "text", scene = "text" },
		{ name = "movie", scene = "movie", wait = 1 },
		{ name = "aft", scene = "aft" },
		{ name = "lines", scene = "lines" },
		{ name = "model", scene = "model" },
		{ name = "lighting", scene = "lighting" },
		{ name = "banners", scene = "banners" },
		{ name = "npot", scene = "npot" },
		{ name = "gameplay-default-cel", gameplay = true, skins = { "default", "cel" },
		  shots = { [420] = "intro", [1500] = "notes" } },
		{ name = "gameplay-metal-vivid-celshaded", gameplay = true, skins = { "metal", "vivid" },
		  celshade = true, shots = { [1500] = "notes" } },
		-- Plays the whole song with RenderProfiler on (Phase 0.2 baseline).
		{ name = "perf", gameplay = true, skins = { "default", "cel" }, profile = true },
		-- Uses the stage stats from the last gameplay step.
		{ name = "graph", scene = "graph" },
	},
}

-- Options from rendertest.py.
local config = IniFile.ReadFile("/Save/RenderTest.ini").RenderTest or {}
if config.SkipPerf == 1 then
	for i, step in ipairs(RenderTest.steps) do
		if step.profile then table.remove(RenderTest.steps, i) break end
	end
end

function RenderTest.Next()
	RenderTest.index = RenderTest.index + 1
	local step = RenderTest.steps[RenderTest.index]
	-- Screenshots read a copy of the back buffer taken every frame (the front
	-- buffer reads back black under Xvfb). Off for the perf run: it costs a
	-- readback per frame.
	PREFSMAN:SetPreference("ScreenshotBackBuffer", not (step and step.profile))
	return step
end

function RenderTest.Current()
	return RenderTest.steps[RenderTest.index]
end

function RenderTest.Shot(name)
	local ok, path = SaveScreenshot(nil, false, false, name .. "__", "")
	Trace("RenderTest: screenshot " .. name .. " -> " .. tostring(path) .. " ok=" .. tostring(ok))
end

-- rendertest.py collects these lines into textures.txt.
function RenderTest.ReportTexture(tex)
	if not tex then return end
	Trace(("RenderTest: texture %s image %dx%d texture %dx%d"):format(
		tex:GetPath(), tex:GetImageWidth(), tex:GetImageHeight(),
		tex:GetTextureWidth(), tex:GetTextureHeight()))
end

function RenderTest.SetupGameplay(step)
	PREFSMAN:SetPreference("CelShadeModels", step.celshade == true)
	PREFSMAN:SetPreference("RenderProfiler", step.profile and 2 or 0)
	local song = SONGMAN:FindSong(SONG)
	assert(song, "RenderTest: song not found: " .. SONG)
	GAMESTATE:Reset()
	GAMESTATE:SetCurrentPlayMode("PlayMode_Regular")
	GAMESTATE:JoinPlayer(PLAYER_1)
	GAMESTATE:JoinPlayer(PLAYER_2)
	GAMESTATE:SetCurrentStyle("versus")
	GAMESTATE:SetCurrentSong(song)
	local steps = song:GetOneSteps("StepsType_Dance_Single", "Difficulty_Challenge")
	for i, pn in ipairs({ PLAYER_1, PLAYER_2 }) do
		GAMESTATE:SetCurrentSteps(pn, steps)
		GAMESTATE:SetCharacter(pn, "rendertest")
		local ps = GAMESTATE:GetPlayerState(pn)
		ps:SetPlayerOptions("ModsLevel_Preferred", "1.5x, " .. step.skins[i])
	end
end
