-- Song banners: from the low-res image cache (16-bit textures) on the left,
-- full-res on the right. Only the StepMania 5 group, so every machine shows
-- the same songs. scaletoclipped wraps texture coordinates; with padded
-- textures the full-res banners' edges sample padding.
local af = Def.ActorFrame{}
for i, song in ipairs(SONGMAN:GetSongsInGroup("StepMania 5")) do
	local path = song:GetBannerPath()
	if path then
		local y = _screen.h * (i - 0.5) / 3.3
		af[#af+1] = Def.Banner{
			InitCommand=function(self)
				self:LoadFromCachedBanner(path):scaletoclipped(400, 125):xy(_screen.w * 0.26, y)
			end,
			OnCommand=function(self) RenderTest.ReportTexture(self:GetTexture()) end }
		af[#af+1] = Def.Banner{
			InitCommand=function(self)
				self:Load(path):scaletoclipped(400, 125):xy(_screen.w * 0.74, y)
			end,
			OnCommand=function(self) RenderTest.ReportTexture(self:GetTexture()) end }
	end
end
return af
