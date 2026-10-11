// Stands in for the fixed-function pipeline (RageDisplay_OGL.cpp,
// UseDrawProgram), and is the vertex stage of the effect mode shaders.
// Inputs, matrices, light and material are still the GL built-ins.

// [1].x, [1].y: sphere map texgen on texture units 1 and 2
// [1].z: lighting (0 off, 1 on without lights, 2 on with light 0)
// [1].w: per-vertex texture matrix scaling (compiled models)
uniform ivec4 State[2];

// How much of the texture matrix applies per vertex (RageModelVertex).
attribute vec4 TextureMatrixScale;

varying vec4 vColor;
varying vec4 vTex0;
varying vec4 vTex1;

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
		gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
		vec4 scaled = gl_TextureMatrix[0] * gl_MultiTexCoord0;
		vTex0 = scaled * TextureMatrixScale +
			gl_MultiTexCoord0 * (vec4(1.0) - TextureMatrixScale);
		vTex1 = gl_TextureMatrix[1] * gl_MultiTexCoord1;
		vColor = clamp(gl_Color, 0.0, 1.0);
		return;
	}

	// ftransform matches fixed-function positions exactly.
	gl_Position = ftransform();

	// GL_NORMALIZE is off, so fixed function uses the normal unnormalized.
	vec3 normal = gl_NormalMatrix * gl_Normal;

	vec4 color = gl_Color;
	if (State[1].z != 0)
	{
		// Emission + material ambient * light model ambient
		color = gl_FrontLightModelProduct.sceneColor;
		if (State[1].z == 2)
		{
			// Light 0 is directional; non-local viewer.
			vec3 L = normalize(gl_LightSource[0].position.xyz);
			float NdotL = dot(normal, L);
			color += gl_FrontLightProduct[0].ambient;
			color += max(NdotL, 0.0) * gl_FrontLightProduct[0].diffuse;
			// Specular only where the clamped N.L is nonzero.
			if (NdotL > 0.0)
			{
				vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
				float NdotH = max(dot(normal, H), 0.0);
				float shininess = gl_FrontMaterial.shininess;
				float s = shininess == 0.0 ? 1.0 : pow(NdotH, shininess);
				color += s * gl_FrontLightProduct[0].specular;
			}
		}
		color.a = gl_FrontMaterial.diffuse.a;
	}
	// Fixed function clamps vertex colors (glColor can exceed 1).
	vColor = clamp(color, 0.0, 1.0);

	vec4 tex0 = gl_MultiTexCoord0;
	vec4 tex1 = gl_MultiTexCoord1;
	if (State[1].x != 0 || State[1].y != 0)
	{
		vec3 eye = normalize((gl_ModelViewMatrix * gl_Vertex).xyz);
		if (State[1].x != 0)
			tex0 = SphereMap(tex0, eye, normal);
		if (State[1].y != 0)
			tex1 = SphereMap(tex1, eye, normal);
	}
	vTex0 = gl_TextureMatrix[0] * tex0;
	vTex1 = gl_TextureMatrix[1] * tex1;
}
