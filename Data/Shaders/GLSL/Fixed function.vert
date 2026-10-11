// Stands in for the fixed-function pipeline (RageDisplay_OGL.cpp,
// UseShaderPipeline). Inputs and matrices are still the GL built-ins.

varying vec4 vColor;
varying vec4 vTex0;
varying vec4 vTex1;

void main(void)
{
	// ftransform matches fixed-function positions exactly.
	gl_Position = ftransform();
	// Fixed function clamps vertex colors (glColor can exceed 1).
	vColor = clamp(gl_Color, 0.0, 1.0);
	vTex0 = gl_TextureMatrix[0] * gl_MultiTexCoord0;
	vTex1 = gl_TextureMatrix[1] * gl_MultiTexCoord1;
}
