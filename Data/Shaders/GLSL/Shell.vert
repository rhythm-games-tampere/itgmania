in vec4 Position;
in vec3 Normal;

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;

void main() {
	vec4 position = vec4(Position.xyz + Normal * 0.125, 1.0);
	gl_Position = ProjectionMatrix * (ModelViewMatrix * position);
}
