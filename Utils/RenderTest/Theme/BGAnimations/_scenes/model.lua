-- 3D models with fixed-function lighting and materials, using the vivid
-- noteskin's arrow model. Gameplay covers the cel-shaded dancing character.
local model = "/NoteSkins/dance/vivid/model.txt"

local function arrow(x, y, rx, ry, extra)
	return Def.Model{ Meshes=model, Materials=model, Bones=model,
		InitCommand=function(self)
			self:xy(x, y):zoom(2.5):rotationx(rx):rotationy(ry)
			if extra then extra(self) end
		end }
end

return Def.ActorFrame{
	InitCommand=function(self) self:fov(60) end,
	-- Unlit.
	arrow(_screen.w * 0.2, _screen.h * 0.3, 30, 40),
	-- Lit, default light.
	Def.ActorFrame{
		Lighting=true,
		InitCommand=function(self)
			self:SetAmbientLightColor(color("0.2,0.2,0.3,1"))
				:SetDiffuseLightColor(color("1,0.9,0.8,1"))
				:SetSpecularLightColor(color("1,1,1,1"))
				:SetLightDirection({ -0.5, -0.5, 1 })
		end,
		arrow(_screen.w * 0.5, _screen.h * 0.3, 30, 40),
		arrow(_screen.w * 0.8, _screen.h * 0.3, -30, -30, function(s) s:diffuse(color("1,0.4,0.4,1")) end),
		arrow(_screen.w * 0.35, _screen.h * 0.72, 50, 10, function(s) s:zoom(3) end),
		arrow(_screen.w * 0.65, _screen.h * 0.72, 10, 150, function(s) s:glow(color("0,1,0,0.4")) end),
	},
}
