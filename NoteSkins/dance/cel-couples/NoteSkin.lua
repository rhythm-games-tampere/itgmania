--I am the bone of my noteskin
--Arrows are my body, and explosions are my blood
--I have created over a thousand noteskins
--Unknown to death
--Nor known to life
--Have withstood pain to create many noteskins
--Yet these hands will never hold anything
--So as I pray, Unlimited Stepman Works

local ret = ... or {}

--Defining on which direction the other directions should be bassed on
--This will let us use less files which is quite handy to keep the noteskin directory nice
--Do remember this will Redirect all the files of that Direction to the Direction its pointed to
local ButtonRedir =
{
	Up = "Down",
	Down = "Down",
	Left = "Down",
	Right = "Down",
	UpLeft = "Down",
	UpRight = "Down",
}

local ElementRedir =
{

	["Hold Head Active"] = "Hold Head Active",
	["Hold Head Inactive"] = "Hold Head Active",
	["Roll Head Active"] = "Hold Head Active",
	["Roll Head Inactive"] = "Hold Head Active",
	["Tap Fake"] = "Tap Note",
	["Tap Note"] = "Tap Note",
}

-- Parts of noteskins which we want to rotate
local PartsToRotate =
{
	["Hold Explosion"] = true,
	["Hold Head Active"] = true,
	["Hold Head Inactive"] = true,
	["Receptor"] = true,
	["Roll Explosion"] = true,
	["Roll Head Active"] = true,
	["Roll Head Inactive"] = true,
	["Tap Addition"] = true,
	["Tap Explosion Bright W1"] = true,
	["Tap Explosion Bright W2"] = true,
	["Tap Explosion Bright W3"] = true,
	["Tap Explosion Bright W4"] = true,
	["Tap Explosion Bright W5"] = true,
	["Tap Explosion Dim W1"] = true,
	["Tap Explosion Dim W2"] = true,
	["Tap Explosion Dim W3"] = true,
	["Tap Explosion Dim W4"] = true,
	["Tap Explosion Dim W5"] = true,
	["Tap Fake"] = true,
	["Tap Lift"] = true,
	["Tap Note"] = true,
}

local PartsToColor =
{
	["Hold Body Active"] = true,
	["Hold Body Inactive"] = true,
	["Hold Bottomcap Active"] = true,
	["Hold Bottomcap Inactive"] = true,
	["Hold Tail Active"] = true,
	["Hold Tail Inactive"] = true,
	["Hold Topcap Active"] = true,
	["Hold Topcap Inactive"] = true,
	["Roll Body Active"] = true,
	["Roll Body Inactive"] = true,
	["Roll Bottomcap Active"] = true,
	["Roll Bottomcap Inactive"] = true,
	["Roll Tail Active"] = true,
	["Roll Tail Inactive"] = true,
	["Roll Topcap Active"] = true,
	["Roll Topcap Inactive"] = true,
}

-- Defined the parts to be rotated at which degree
local Rotate =
{
	Up = 180,
	Down = 0,
	Left = 90,
	Right = -90,
	UpLeft = 135,
	UpRight = 225,
}

-- Parts that should be Redirected to _Blank.png
-- you can add/remove stuff if you want
local Blank =
{
	["Hold Tail Active"] = true,
	["Hold Tail Inactive"] = true,
	["Hold Topcap Active"] = true,
	["Hold Topcap Inactive"] = true,
	["Roll Tail Active"] = true,
	["Roll Tail Inactive"] = true,
	["Roll Topcap Active"] = true,
	["Roll Topcap Inactive"] = true,
	["Tap Explosion Bright"] = true,
	["Tap Explosion Dim"] = true,
}

local function NoFrames(NumFrames, Seconds)
	local Frames = {}
	for i = 0,NumFrames-1 do
		Frames[#Frames+1] = {
			Frame = 0,
			Delay = (1/NumFrames)*Seconds
		}
	end
	return Frames
end

ret.Load = function()
	local sButton = Var "Button"
	local sElement = Var "Element"

	if Blank[sElement] then
		-- Return a blank element.  If SpriteOnly is set,
		-- we need to return a sprite; otherwise just return
		-- a dummy actor.
		local t
		if Var "SpriteOnly" then
			t = LoadActor("_blank")
		else
			t = Def.Actor {}
		end
		return t .. {
			cmd(visible,false)
		}
	end

	local rotation = PartsToRotate[sElement] and Rotate[sButton] or nil
	local tailColor = NOTESKIN:GetMetric('NoteDisplay', 'VariantColor')

	sElement = ElementRedir[sElement] or sElement
	sButton = ButtonRedir[sButton] or sButton

	if sElement == "Tap Note" then
		return Def.ActorFrame {
			Def.Sprite {
				Texture = NOTESKIN:GetPath('_Down', 'Tails'),
				Frames = Sprite.LinearFrames(32, 1),
				InitCommand = function(self)
					self:valign(0)
					self:diffuse(color(tailColor))
				end,
			},
			Def.Sprite {
				Texture = NOTESKIN:GetPath('_Down', 'Tap Note'),
				Frames = Sprite.LinearFrames(32, 1),
				BaseRotationZ = rotation,
			},
		}
	elseif sElement == "Hold Head Active" then
		return Def.Sprite {
			Texture = NOTESKIN:GetPath('_Down', 'Tap Note'),
			Frames = Sprite.LinearFrames(32, 1),
			BaseRotationZ = rotation,
		}
	elseif sElement == "Tap Lift" then
		return Def.Sprite {
			Texture = NOTESKIN:GetPath('_Down', 'Tap Lift'),
			Frames = Sprite.LinearFrames(64, 2),
		}
	elseif PartsToColor[sElement] then
		return Def.Sprite {
			Texture = NOTESKIN:GetPath(sButton, sElement),
			BaseRotationZ = rotation,
		}
	else
		local sPath = NOTESKIN:GetPath(sButton, sElement)
		local t = LoadActor(sPath)
		t.BaseRotationZ = rotation
		return t
	end
end

return ret
