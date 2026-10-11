-- Every two-texture EffectMode on an ActorMultiTexture, plus single-texture
-- sprites with EffectModes. YUYV422 is in movie.lua, DistanceField in text.lua.
local gradient = THEME:GetPathG("", "rt gradient")
local checker = THEME:GetPathG("", "rt checker")
local modes = { "Normal", "Unpremultiply", "ColorBurn", "ColorDodge", "VividLight",
	"HardMix", "Overlay", "Screen" }

local tile = math.floor(_screen.w / 9)
local af = Def.ActorFrame{
	Def.Sprite{ Name="TexA", Texture=gradient, InitCommand=function(self) self:visible(false) end },
	Def.Sprite{ Name="TexB", Texture=checker, InitCommand=function(self) self:visible(false) end },
}

local function label(text, x, y)
	return Def.BitmapText{ Font="Common Normal", Text=text,
		InitCommand=function(self) self:xy(x, y):zoom(0.45) end }
end

for i, m in ipairs(modes) do
	local x = tile * (i - 0.5) + tile / 2
	local y = _screen.h * 0.3
	af[#af+1] = Def.ActorMultiTexture{
		OnCommand=function(self)
			local a = self:GetParent():GetChild("TexA"):GetTexture()
			local b = self:GetParent():GetChild("TexB"):GetTexture()
			self:ClearTextures()
			self:AddTexture(b)
			self:AddTexture(a)
			self:SetEffectMode("EffectMode_" .. m)
			self:SetSizeFromTexture(b)
			self:xy(x, y):zoomto(tile - 4, tile - 4)
		end,
	}
	af[#af+1] = label(m, x, y + tile / 2 + 8)

	af[#af+1] = Def.Sprite{ Texture=gradient,
		InitCommand=function(self)
			self:xy(x, _screen.h * 0.7):zoomto(tile - 4, tile - 4):SetEffectMode("EffectMode_" .. m)
		end }
	af[#af+1] = label("Sprite " .. m, x, _screen.h * 0.7 + tile / 2 + 8)
end

return af
