#version 140

// Slice-1 GPU plumbing spike: fullscreen passthrough. The engine feeds a screen-space
// quad (vertexPosition in pixels) through the standard primitive path; we map it to clip
// space the same way world.vert does, and derive a [0,1] sample coordinate from the clip
// position so the pass is independent of the (runtime-sized) target framebuffer.
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
