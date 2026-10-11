//We need this shader to pass the vertex color to the frag shader

in vec4 Position;
in vec4 Color;
in vec4 TexCoord;
in vec3 Normal;

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;

out vec4 vertexColor;
out vec4 texCoords;

void main(void) {
	vec4 position = vec4(Position.xyz + Normal * 0.125, 1.0);
	gl_Position = ProjectionMatrix * (ModelViewMatrix * position);
	texCoords = TexCoord;
	vertexColor = Color;
}
