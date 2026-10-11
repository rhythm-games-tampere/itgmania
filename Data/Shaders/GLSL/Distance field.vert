//We need this shader to pass the vertex color to the frag shader

attribute vec4 Position;
attribute vec4 Color;
attribute vec4 TexCoord;
attribute vec3 Normal;

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;

varying vec4 vertexColor;
varying vec4 texCoords;

void main(void) {
	vec4 position = vec4(Position.xyz + Normal * 0.125, 1.0);
	gl_Position = ProjectionMatrix * (ModelViewMatrix * position);
	texCoords = TexCoord;
	vertexColor = Color;
}
