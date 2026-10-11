// Texture env modes and alpha test of the fixed-function pipeline.

uniform sampler2D Texture1;
uniform sampler2D Texture2;
// x, y: texture units 1 and 2 (0 off, 1 modulate, 2 add, 3 glow)
// z: alpha test (alpha > 1/256)
uniform ivec4 State;

varying vec4 vColor;
varying vec4 vTex0;
varying vec4 vTex1;

vec4 TexEnv(int mode, vec4 previous, vec4 texel)
{
	if (mode == 1)  // GL_MODULATE
		return previous * texel;
	if (mode == 2)  // GL_ADD: alpha still modulates
		return clamp(vec4(previous.rgb + texel.rgb, previous.a * texel.a), 0.0, 1.0);
	// Glow (GL_COMBINE): rgb from the vertex color, alpha vertex * texture
	return vec4(vColor.rgb, vColor.a * texel.a);
}

void main(void)
{
	vec4 color = vColor;
	if (State.x != 0)
		color = TexEnv(State.x, color, texture2DProj(Texture1, vTex0));
	if (State.y != 0)
		color = TexEnv(State.y, color, texture2DProj(Texture2, vTex1));
	if (State.z != 0 && !(color.a > 0.00390625))
		discard;
	gl_FragColor = color;
}
