#version 150

// Slice-2 GPU spread: fullscreen pass. The engine feeds a screen-space quad (vertexPosition in
// pixels); map to clip space via the screenSize uniform (set to the lightmap size by
// setRenderTarget) and derive a [0,1] sample coordinate from the clip position.
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
