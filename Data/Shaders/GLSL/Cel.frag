uniform sampler2D Texture0;
uniform mat4 ModelViewMatrix;
uniform vec4 MaterialAmbient;
uniform vec4 LightPosition;  // light 0, eye space

in vec2 vCoord;
in vec3 vNor;

const vec4 shadowColor = vec4(0.6, 0.75, 0.9, 1.0);

void main() {
	vec3 nor, light;
	nor = normalize(vNor);
	
	vec4 diffuse, specular, color, lightSource;
	float ambient = length(MaterialAmbient.rgb);
	lightSource = LightPosition;
	light = normalize(ModelViewMatrix * lightSource).xyz;
	
	color = texture(Texture0, vCoord.st);
	float intensity = max(dot(light,nor), 0.0);
	if (intensity < 0.5) {
		intensity *= 0.5;
		color *= shadowColor;
	}
	intensity = min(clamp(intensity, ambient, 1.0) + 0.25, 1.0);
	
	FragColor = color * intensity;
	
	// Don't allow transparency. Bad Things will happen.
	FragColor.a = 1.0;
}
