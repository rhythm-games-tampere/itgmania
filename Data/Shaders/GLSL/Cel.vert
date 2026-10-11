attribute vec4 Position;
attribute vec4 TexCoord;
attribute vec3 Normal;

uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionMatrix;

varying vec2 vCoord;
varying vec3 vNor;

void main() {
	vCoord = TexCoord.st;
	vNor = Normal.xyz;

	gl_Position = ProjectionMatrix * (ModelViewMatrix * Position);
}
