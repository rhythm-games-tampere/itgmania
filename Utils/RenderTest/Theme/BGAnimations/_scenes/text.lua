-- Bitmap text (with stroke and per-vertex colours) and distance-field text
-- at several scales and rotations.
local sample = "The quick brown fox 0123456789 !?&"
local af = Def.ActorFrame{}

local rows = {
	{ "Common Normal", 1, 0 },
	{ "Common Normal", 2, 0 },
	{ "rt sdf", 0.6, 0 },
	{ "rt sdf", 1.5, 0 },
	{ "rt sdf", 3, -8 },
}
for i, r in ipairs(rows) do
	af[#af+1] = Def.BitmapText{ Font=r[1], Text=sample,
		InitCommand=function(self)
			self:xy(_screen.cx, _screen.h * (i - 0.4) / (#rows + 0.6)):zoom(r[2]):rotationz(r[3])
			if i == 2 then
				self:strokecolor(color("1,0,0,1")):diffusetopedge(color("1,1,0,1")):diffusebottomedge(color("0,1,1,1"))
			end
			if i == 4 then self:diffuse(color("1,0.5,0,0.8")) end
		end }
end

return af
