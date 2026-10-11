//We need this shader to pass the vertex color to the frag shader

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;

varying vec4 vertexColor;
varying vec4 texCoords;

void main(void) {
	vec4 position = vec4(gl_Vertex.xyz + gl_Normal * 0.125, 1.0);
	gl_Position = ProjectionMatrix * (ModelViewMatrix * position);
	texCoords = gl_MultiTexCoord0;
	vertexColor = gl_Color;
}
