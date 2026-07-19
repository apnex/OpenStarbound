#version 150

// CM-1 merged backdrop compose: screen-space quad (vertexPosition in pixels) mapped to clip
// space, with a [0,1] sample coordinate derived from clip position (target-size independent).
// Identical to lightingPassthrough.vert -- both are full-screen passes over "main".
uniform vec2 screenSize;

in vec2 vertexPosition;
in vec2 vertexTextureCoordinate;
in vec4 vertexColor;
in int vertexData;

out vec2 fragTexCoord;

void main() {
  vec2 clip = vertexPosition / screenSize * 2.0 - 1.0;
  fragTexCoord = clip * 0.5 + 0.5;
  gl_Position = vec4(clip, 0.0, 1.0);
}
