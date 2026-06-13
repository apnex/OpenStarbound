#version 140

// Slice-1 spike: identity passthrough. Slice 3 also uses it as the final COMPOSE pass: read the
// accumulation (spread + point), optionally apply the brightnessLimit cap (proportional,
// hue-preserving, exactly as CellularLightingCalculator::calculate), and write the result the world
// shader consumes. applyCap=false leaves it a pure passthrough.
uniform sampler2D inputTexture;
uniform vec2 inputTextureSize;
uniform bool applyCap;
uniform float brightnessLimit;

in vec2 fragTexCoord;

out vec4 fragColor;

void main() {
  vec4 c = texture(inputTexture, fragTexCoord);
  if (applyCap) {
    float intensity = max(c.r, max(c.g, c.b));
    if (intensity > brightnessLimit)
      c.rgb *= brightnessLimit / intensity;
  }
  // Force alpha = 1.0: the point pass accumulates alpha additively (1.0 per light), so the input
  // alpha can be >> 1. The engine's alpha-blend compose (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
  // would then amplify src by that alpha (and, with clear:false, feed back on the prior frame) ->
  // runaway over-brightness. Writing alpha 1.0 makes the compose a clean replace.
  fragColor = vec4(c.rgb, 1.0);
}
