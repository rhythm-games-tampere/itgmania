-- Lighting and sphere mapping on a model whose meshes don't need the texture
-- matrix scaling shader (which ignores lighting), so they reach the fixed
-- function path. "rt lit model.txt" is the ddr-vivid tap note with specular
-- materials; its "Frame" material has shininess 0 and a "sphere" texture.
local model = THEME:GetPathG("", "rt lit model.txt")

local function note(x, y, rx, ry, extra)
	return Def.Model{ Meshes=model, Materials=model, Bones=model,
		InitCommand=function(self)
			self:xy(x, y):zoom(3):rotationx(rx):rotationy(ry)
			if extra then extra(self) end
		end }
end

local function lit(y, ambient, diffuse, specular, dir)
	return Def.ActorFrame{
		Lighting=true,
		InitCommand=function(self)
			self:SetAmbientLightColor(color(ambient)):SetDiffuseLightColor(color(diffuse))
				:SetSpecularLightColor(color(specular)):SetLightDirection(dir)
		end,
		note(_screen.w * 0.15, y, 20, 30),
		note(_screen.w * 0.38, y, -40, 160),
		-- Scaled: fixed function doesn't renormalize the normals.
		note(_screen.w * 0.62, y, 60, -20, function(s) s:zoom(5) end),
		note(_screen.w * 0.85, y, 10, 70, function(s) s:diffuse(color("0.4,1,0.6,0.8")) end),
	}
end

return Def.ActorFrame{
	InitCommand=function(self) self:fov(60) end,
	lit(_screen.h * 0.2, "0.2,0.2,0.3,1", "1,0.9,0.8,1", "1,1,1,1", { -0.5, -0.5, 1 }),
	lit(_screen.h * 0.5, "0.1,0.1,0.1,1", "0.6,0.6,1,1", "0.5,0.5,0.5,1", { 1, 0.3, 0.2 }),
	-- Unlit, sphere mapped frame.
	note(_screen.w * 0.3, _screen.h * 0.8, 30, 40),
	note(_screen.w * 0.7, _screen.h * 0.8, -20, -60, function(s) s:glow(color("1,1,0,0.3")) end),
}
