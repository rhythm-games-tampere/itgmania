-- ActorFrameTexture render target, drawn back several ways.
local gradient = THEME:GetPathG("", "rt gradient")
local checker = THEME:GetPathG("", "rt checker")

return Def.ActorFrame{
	Def.ActorFrameTexture{
		Name="AFT",
		InitCommand=function(self)
			self:setsize(256, 256):EnableAlphaBuffer(true):SetTextureName("rt aft"):Create()
		end,
		Def.Quad{ InitCommand=function(self) self:xy(128, 128):zoomto(200, 200):diffuse(color("0.2,0.4,1,0.5")) end },
		Def.Sprite{ Texture=checker, InitCommand=function(self) self:xy(96, 96):zoom(1.2) end },
		Def.Sprite{ Texture=gradient, InitCommand=function(self) self:xy(160, 160):rotationz(20):blend("BlendMode_Add") end },
		Def.BitmapText{ Font="Common Normal", Text="AFT", InitCommand=function(self) self:xy(128, 230):zoom(2) end },
	},
	Def.Sprite{ OnCommand=function(self) self:SetTexture(self:GetParent():GetChild("AFT"):GetTexture()):xy(_screen.w * 0.2, _screen.cy) end },
	Def.Sprite{ OnCommand=function(self) self:SetTexture(self:GetParent():GetChild("AFT"):GetTexture()):xy(_screen.w * 0.5, _screen.cy):zoom(1.4):rotationz(-12):diffuse(color("1,0.8,0.8,1")) end },
	Def.Sprite{ OnCommand=function(self) self:SetTexture(self:GetParent():GetChild("AFT"):GetTexture()):xy(_screen.w * 0.82, _screen.cy):zoom(0.5):blend("BlendMode_Add") end },
}
