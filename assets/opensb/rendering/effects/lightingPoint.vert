#version 150

// Slice-3 GPU point lighting: per-light bbox quad. The quad is supplied in lightmap-grid pixel
// coords [lxmin,lymin]-[lxmax,lymax]; map to clip space via screenSize (= grid size, set by
// setRenderTarget). The fragment reconstructs its cell-center blockPos from gl_FragCoord, so no
// varying is needed here.
uniform vec2 screenSize;

in vec2 vertexPosition;
in vec2 vertexTextureCoordinate;
in vec4 vertexColor;
in int vertexData;

void main() {
  gl_Position = vec4(vertexPosition / screenSize * 2.0 - 1.0, 0.0, 1.0);
}
