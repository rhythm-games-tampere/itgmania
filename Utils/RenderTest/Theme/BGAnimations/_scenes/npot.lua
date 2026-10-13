-- Textures whose sizes aren't powers of two: odd widths (16bpp rows that
-- aren't 4-byte aligned), mipmaps, a sprite sheet, a large image, the stretch
-- hint, wrapping texture coordinates and a render target. Each texture's size
-- is logged for rendertest.py's textures.txt.
-- Full paths: theme element lookup would match "rt npot" to every file here.
local function G(name) return "/" .. THEME:GetCurrentThemeDirectory() .. "Graphics/" .. name .. ".png" end
local w, h = _screen.w, _screen.h

local function sprite(name, x, y, f)
	return Def.Sprite{ Texture=G(name),
		InitCommand=function(self)
			self:xy(x, y)
			if f then f(self) end
		end,
		OnCommand=function(self) RenderTest.ReportTexture(self:GetTexture()) end,
	}
end

local af = Def.ActorFrame{
	sprite("rt npot", w * 0.2, h * 0.17),
	sprite("rt npot 16bpp", w * 0.55, h * 0.17),
	sprite("rt npot mipmaps", w * 0.84, h * 0.17, function(s) s:zoom(0.3) end),
	sprite("rt npot big", w * 0.84, h * 0.4, function(s) s:zoomto(240, 135) end),
	sprite("rt npot (stretch)", w * 0.84, h * 0.62, function(s) s:zoom(1.5) end),
	-- Texture coordinates are fractions of the texture, so with padding this
	-- shows padding between the repeats; without it, a seamless tiling.
	sprite("rt npot", w * 0.2, h * 0.5, function(s)
		s:zoomto(260, 150):customtexturerect(0.1, 0.2, 2.6, 1.7)
	end),
}

for i = 0, 5 do
	af[#af+1] = sprite("rt npot frames 3x2", w * (0.4 + (i % 3) * 0.1), h * (0.4 + math.floor(i / 3) * 0.14),
		function(s) s:animate(false):setstate(i):zoom(1.3) end)
end

af[#af+1] = Def.ActorFrameTexture{
	Name="AFT",
	InitCommand=function(self)
		self:setsize(300, 170):EnableAlphaBuffer(true):SetTextureName("rt npot aft"):Create()
	end,
	-- Without render targets (D3D) the children draw on screen, over the rest.
	OnCommand=function(self) self:visible(self:GetTexture():GetTextureWidth() > 0) end,
	Def.Quad{ InitCommand=function(self) self:xy(150, 85):zoomto(300, 170):diffuse(color("0.6,0.1,0.3,1")) end },
	Def.Sprite{ Texture=G("rt npot"), InitCommand=function(self) self:xy(150, 85):zoom(0.8) end },
	Def.BitmapText{ Font="Common Normal", Text="AFT 300x170", InitCommand=function(self) self:xy(150, 150) end },
}
af[#af+1] = Def.Sprite{
	OnCommand=function(self)
		local tex = self:GetParent():GetChild("AFT"):GetTexture()
		self:SetTexture(tex):xy(w * 0.3, h * 0.78)
		RenderTest.ReportTexture(tex)
	end }
af[#af+1] = Def.Sprite{
	OnCommand=function(self)
		self:SetTexture(self:GetParent():GetChild("AFT"):GetTexture())
			:xy(w * 0.65, h * 0.82):zoomto(200, 100):customtexturerect(0, 0, 1.5, 1.5)
	end }

return af
