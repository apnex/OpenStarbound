#version 140

// Slice-1 spike: identity passthrough. Slice 3 also uses it as the final COMPOSE pass: read the
// accumulation (spread + point), optionally apply the brightnessLimit cap (proportional,
// hue-preserving, exactly as CellularLightingCalculator::calculate), and write the result the world
// shader consumes. applyCap=false leaves it a pure passthrough.
uniform sampler2D inputTexture;
uniform vec2 inputTextureSize;
uniform bool applyCap;
uniform float brightnessLimit;
uniform float brightnessScale;   // GPU-only final tone (1.0 = no change); tune to match a reference build
uniform bool tonemap;            // CDL: value-preserving highlight rolloff instead of the hard cap

in vec2 fragTexCoord;

out vec4 fragColor;

void main() {
  vec4 c = texture(inputTexture, fragTexCoord);
  if (applyCap) {
    float intensity = max(c.r, max(c.g, c.b));
    if (tonemap) {
      // CDL value-preserving highlight rolloff (mirrors Star::tonemapHighlights): identity for
      // intensity <= 1, smooth compression of the excess toward the white-point (no hard clip).
      if (intensity > 1.0) {
        float e = intensity - 1.0;
        float k = max(brightnessLimit - 1.0, 1.0e-4);
        c.rgb *= (1.0 + k * e / (k + e)) / intensity;
      }
    } else if (intensity > brightnessLimit) {
      c.rgb *= brightnessLimit / intensity;   // legacy proportional clamp (unchanged)
    }
  }
  c.rgb *= brightnessScale;   // applied after the cap: uniform final tone-down/up
  // Force alpha = 1.0: the point pass accumulates alpha additively (1.0 per light), so the input
  // alpha can be >> 1. The engine's alpha-blend compose (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
  // would then amplify src by that alpha (and, with clear:false, feed back on the prior frame) ->
  // runaway over-brightness. Writing alpha 1.0 makes the compose a clean replace.
  fragColor = vec4(c.rgb, 1.0);
}
