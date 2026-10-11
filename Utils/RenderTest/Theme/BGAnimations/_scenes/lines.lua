-- Line strips (smooth wide lines in the GL renderer), the groove radar and the
-- other ActorMultiVertex draw modes.
local af = Def.ActorFrame{}

-- Zig-zag line strips with increasing width.
for i, width in ipairs({ 1, 2, 4, 8 }) do
	local verts = {}
	for k = 0, 12 do
		local c = { k / 12, 1 - k / 12, 0.5 + i / 8, 1 }
		verts[#verts+1] = { { k * 28, (k % 2) * 40, 0 }, c }
	end
	af[#af+1] = Def.ActorMultiVertex{
		InitCommand=function(self)
			self:xy(30, _screen.h * 0.1 + (i - 1) * 60)
			self:SetDrawState{ Mode="DrawMode_LineStrip" }:SetLineWidth(width):SetVertices(verts)
		end }
end

af[#af+1] = Def.GrooveRadar{
	InitCommand=function(self)
		self:xy(_screen.w * 0.7, _screen.h * 0.32):zoom(1.6)
		self:SetFromValues(PLAYER_1, { 0.9, 0.4, 0.7, 0.2, 0.6 })
		self:SetFromValues(PLAYER_2, { 0.3, 0.8, 0.5, 0.95, 0.1 })
	end }

-- Fan, strip, triangles and quad strip.
local function shape(mode, x, verts)
	return Def.ActorMultiVertex{
		InitCommand=function(self)
			self:xy(x, _screen.h * 0.78):SetDrawState{ Mode=mode }:SetVertices(verts)
		end }
end
local R, G, B, W = { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 1, 0.5 }
af[#af+1] = shape("DrawMode_Fan", _screen.w * 0.15,
	{ { { 0, 0, 0 }, W }, { { -60, -40, 0 }, R }, { { 0, -70, 0 }, G }, { { 60, -40, 0 }, B }, { { 60, 40, 0 }, R } })
af[#af+1] = shape("DrawMode_Strip", _screen.w * 0.4,
	{ { { -60, -40, 0 }, R }, { { -60, 40, 0 }, G }, { { 0, -40, 0 }, B }, { { 0, 40, 0 }, W }, { { 60, -40, 0 }, R }, { { 60, 40, 0 }, G } })
af[#af+1] = shape("DrawMode_Triangles", _screen.w * 0.65,
	{ { { -60, 40, 0 }, R }, { { 0, -50, 0 }, G }, { { 60, 40, 0 }, B } })
af[#af+1] = shape("DrawMode_QuadStrip", _screen.w * 0.88,
	{ { { -50, -40, 0 }, R }, { { -50, 40, 0 }, G }, { { 0, -30, 0 }, B }, { { 0, 30, 0 }, W }, { { 50, -40, 0 }, R }, { { 50, 40, 0 }, G } })

return af
