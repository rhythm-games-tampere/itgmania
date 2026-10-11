in vec4 Position;
in vec4 TexCoord;
in vec3 Normal;

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;

out vec2 vCoord;
out vec3 vNor;

void main() {
	vCoord = TexCoord.st;
	vNor = Normal.xyz;

	gl_Position = ProjectionMatrix * (ModelViewMatrix * Position);
}
