-- Movie texture (decoded as YUYV422 where the renderer supports it). Paused
-- until the decoder has buffered frames, so the shown frame doesn't depend on
-- decoding speed.
return Def.ActorFrame{
	Def.Sprite{ Texture="rt movie.avi",
		InitCommand=function(self) self:Center():zoom(3):GetTexture():rate(0) end,
		OnCommand=function(self) RenderTest.ReportTexture(self:GetTexture()) end,
		RenderTestStartMessageCommand=function(self) self:GetTexture():rate(1) end,
	},
	Def.Sprite{ Texture="rt movie.avi",
		InitCommand=function(self)
			self:xy(_screen.w * 0.15, _screen.h * 0.15):zoom(0.75):rotationz(15)
		end },
}
