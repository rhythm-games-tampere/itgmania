// Stands in for the fixed-function pipeline (RageDisplay_OGL.cpp,
// UseDrawProgram), and is the vertex stage of the effect mode shaders.

// Vertex inputs, bound to fixed locations by LoadProgram. Both texture
// units use TexCoord.
in vec4 Position;
in vec4 Color;
in vec4 TexCoord;
in vec3 Normal;

// [1].x, [1].y: sphere map texgen on texture units 1 and 2
// [1].z: lighting (0 off, 1 on without lights, 2 on with light 0)
// [1].w: per-vertex texture matrix scaling (compiled models)
uniform ivec4 State[2];

// How much of the texture matrix applies per vertex (RageModelVertex).
in vec4 TextureMatrixScale;

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;
uniform mat3 NormalMatrix;  // inverse transpose of the modelview's 3x3
uniform mat4 TextureMatrix[2];

// Front material, and light 0: directional, position in eye space.
uniform vec4 MaterialEmission;
uniform vec4 MaterialAmbient;
uniform vec4 MaterialDiffuse;
uniform vec4 MaterialSpecular;
uniform float MaterialShininess;
uniform vec4 LightAmbient;
uniform vec4 LightDiffuse;
uniform vec4 LightSpecular;
uniform vec4 LightPosition;

// GL_LIGHT_MODEL_AMBIENT, never changed from its default.
const vec4 LightModelAmbient = vec4(0.2, 0.2, 0.2, 1.0);

out vec4 vColor;
out vec4 vTex0;
out vec4 vTex1;

// GL_SPHERE_MAP: s and t from the eye-space reflection vector.
vec4 SphereMap(vec4 tex, vec3 eye, vec3 normal)
{
	vec3 r = eye - 2.0 * normal * dot(normal, eye);
	float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
	return vec4(r.x / m + 0.5, r.y / m + 0.5, tex.z, tex.w);
}

void main(void)
{
	if (State[1].w != 0)
	{
		// What the old "Texture matrix scaling" shader did: no lighting
		// (a known bug, kept so models look the same) and no sphere map.
		gl_Position = ProjectionMatrix * (ModelViewMatrix * Position);
		vec4 scaled = TextureMatrix[0] * TexCoord;
		vTex0 = scaled * TextureMatrixScale +
			TexCoord * (vec4(1.0) - TextureMatrixScale);
		vTex1 = TextureMatrix[1] * TexCoord;
		vColor = clamp(Color, 0.0, 1.0);
		return;
	}

	vec4 eyePosition = ModelViewMatrix * Position;
	gl_Position = ProjectionMatrix * eyePosition;

	// GL_NORMALIZE is off, so fixed function uses the normal unnormalized.
	vec3 normal = NormalMatrix * Normal;

	vec4 color = Color;
	if (State[1].z != 0)
	{
		// Emission + material ambient * light model ambient
		color = MaterialEmission + MaterialAmbient * LightModelAmbient;
		if (State[1].z == 2)
		{
			// Light 0 is directional; non-local viewer.
			vec3 L = normalize(LightPosition.xyz);
			float NdotL = dot(normal, L);
			color += LightAmbient * MaterialAmbient;
			color += max(NdotL, 0.0) * LightDiffuse * MaterialDiffuse;
			// Specular only where the clamped N.L is nonzero.
			if (NdotL > 0.0)
			{
				vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
				float NdotH = max(dot(normal, H), 0.0);
				float s = MaterialShininess == 0.0 ? 1.0 : pow(NdotH, MaterialShininess);
				color += s * LightSpecular * MaterialSpecular;
			}
		}
		color.a = MaterialDiffuse.a;
	}
	// Fixed function clamps vertex colors (glColor can exceed 1).
	vColor = clamp(color, 0.0, 1.0);

	vec4 tex0 = TexCoord;
	vec4 tex1 = TexCoord;
	if (State[1].x != 0 || State[1].y != 0)
	{
		vec3 eye = normalize(eyePosition.xyz);
		if (State[1].x != 0)
			tex0 = SphereMap(tex0, eye, normal);
		if (State[1].y != 0)
			tex1 = SphereMap(tex1, eye, normal);
	}
	vTex0 = TextureMatrix[0] * tex0;
	vTex1 = TextureMatrix[1] * tex1;
}
