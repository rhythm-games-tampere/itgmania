-- Every BlendMode over the same destination, then Glow and the AMT texture
-- modes (Modulate, Glow, Add) plus diffuse/glow on plain sprites.
local gradient = THEME:GetPathG("", "rt gradient")
local checker = THEME:GetPathG("", "rt checker")
local blends = { "Normal", "Add", "Subtract", "Modulate", "CopySrc", "AlphaMask",
	"AlphaKnockOut", "AlphaMultiply", "WeightedMultiply", "InvertDest", "NoEffect" }

local tile = math.floor(_screen.w / 12)
local af = Def.ActorFrame{}

local function label(text, x, y)
	return Def.BitmapText{ Font="Common Normal", Text=text,
		InitCommand=function(self) self:xy(x, y):zoom(0.35) end }
end

for i, b in ipairs(blends) do
	local x = tile * (i - 0.5) + tile / 2
	local y = _screen.h * 0.22
	af[#af+1] = Def.Sprite{ Texture=checker, InitCommand=function(self) self:xy(x, y):zoomto(tile - 4, tile - 4) end }
	af[#af+1] = Def.Sprite{ Texture=gradient,
		InitCommand=function(self) self:xy(x, y):zoomto(tile - 4, tile - 4):diffusealpha(0.9):blend("BlendMode_" .. b) end }
	af[#af+1] = label(b, x, y + tile / 2 + 8)
end

-- Row 2: glow and diffuse variations on a plain sprite.
local row2 = {
	{ "glow red", function(s) s:glow(color("1,0,0,0.7")) end },
	{ "glow white", function(s) s:glow(color("1,1,1,0.4")) end },
	{ "diffuse", function(s) s:diffuse(color("0.3,0.8,1,1")) end },
	{ "gradient", function(s) s:diffuseupperleft(color("1,0,0,1")):diffuselowerright(color("0,0,1,0.5")) end },
	{ "add+glow", function(s) s:blend("BlendMode_Add"):glow(color("0,1,0,0.5")) end },
	{ "rotated", function(s) s:rotationz(30):zoom(0.6) end },
	{ "3D rot", function(s) s:rotationy(50):rotationx(20) end },
	{ "texcoord", function(s) s:customtexturerect(0, 0, 2, 2):texturewrapping(true) end },
	{ "unfiltered", function(s) s:customtexturerect(0, 0, 0.25, 0.25):SetTextureFiltering(false) end },
}
for i, r in ipairs(row2) do
	local x = tile * (i - 0.5) + tile / 2
	local y = _screen.h * 0.52
	af[#af+1] = Def.Sprite{ Texture=checker, InitCommand=function(self) self:xy(x, y):zoomto(tile - 4, tile - 4) end }
	af[#af+1] = Def.Sprite{ Texture=gradient,
		InitCommand=function(self) self:xy(x, y):zoomto(tile - 4, tile - 4); r[2](self) end }
	af[#af+1] = label(r[1], x, y + tile / 2 + 8)
end

-- Row 3: ActorMultiTexture with each TextureMode on the second stage.
local modes = { "Modulate", "Glow", "Add" }
af[#af+1] = Def.Sprite{ Name="TexA", Texture=gradient, InitCommand=function(self) self:visible(false) end }
af[#af+1] = Def.Sprite{ Name="TexB", Texture=checker, InitCommand=function(self) self:visible(false) end }
for i, m in ipairs(modes) do
	local x = tile * (i - 0.5) + tile / 2
	local y = _screen.h * 0.82
	af[#af+1] = Def.ActorMultiTexture{
		OnCommand=function(self)
			local a = self:GetParent():GetChild("TexA"):GetTexture()
			local b = self:GetParent():GetChild("TexB"):GetTexture()
			self:ClearTextures()
			self:AddTexture(b)
			self:AddTexture(a)
			self:SetTextureMode(1, "TextureMode_" .. m)
			self:SetSizeFromTexture(b)
			self:xy(x, y):zoomto(tile - 4, tile - 4)
		end,
	}
	af[#af+1] = label("AMT " .. m, x, y + tile / 2 + 8)
end

return af
