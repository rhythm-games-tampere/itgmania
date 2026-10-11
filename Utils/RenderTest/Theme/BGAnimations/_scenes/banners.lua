-- Song banners from the low-res image cache (16-bit textures). Full-res
-- banners aren't shown: their left edge differs between runs of the same
-- build (likely uninitialized texture padding being sampled).
local af = Def.ActorFrame{}
for i, song in ipairs(SONGMAN:GetAllSongs()) do
	local path = song:GetBannerPath()
	if path then
		af[#af+1] = Def.Banner{
			InitCommand=function(self)
				self:LoadFromCachedBanner(path):scaletoclipped(480, 150)
					:xy(_screen.cx, _screen.h * (i - 0.5) / 3.3)
			end }
	end
end
return af
