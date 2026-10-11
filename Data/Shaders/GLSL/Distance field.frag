uniform sampler2D Texture1;
// [0].z: alpha test (see Fixed function.frag)
uniform ivec4 State[2];

in vec4 texCoords;
in vec4 vertexColor;

float aastep(float threshold, float dist) {
	float afwidth = 0.75 * length(vec2(dFdx(dist), dFdy(dist)));
	return smoothstep(threshold - afwidth, threshold + afwidth, dist);
}

float median(float r, float g, float b) {
	return max(min(r, g), min(max(r, g), b));
}

const float distanceThreshold = 0.5;

float SampleDistance(vec2 tc) {
	vec3 texel = texture(Texture1, tc).rgb;
	float dist = median(texel.r, texel.g, texel.b);
	return aastep(distanceThreshold, dist);
}

void main(void)
{
	float dfValue = SampleDistance(texCoords.st);
	float alpha = vertexColor.a * dfValue;
	if (State[0].z != 0 && !(alpha > 0.00390625))
		discard;
	FragColor = vec4(vertexColor.rgb, alpha);
}
