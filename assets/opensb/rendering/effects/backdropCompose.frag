#version 150

// CM-1: one full-screen pass replacing the two sequential composites into "main".
//
//   E = envTexture      (env cache; the opaque backdrop -- today's env compose forces alpha=1)
//   P = parallaxTexture  (parallax cache; premultiplied coverage -- today's premultiplied-over compose)
//
// The two sequential composites were:
//     main = E                          (opaque replace onto cleared main)
//     main = P.rgb + main * (1 - P.a)   (premultiplied-over)
// which composes to the single expression below. The caller (mergedCompose) sets BlendMode::None so this is a
// full replace; env covers every pixel so no clear is needed. Output alpha is 1 (main is opaque).
//
// IDENTITY-TRANSFORM ASSUMPTION (adversarial review): the env + parallax backdrop composites have ALWAYS used
// identity params (applyCap=false, brightnessScale=1, tonemap=false) -- unlike the lighting compose, which caps
// and tonemaps on the same lightingPassthrough effect. This shader hardcodes that identity. If the backdrop
// composites ever gain a non-identity cap/scale/tonemap, this shader (and the non-merge path in BackdropPass)
// must change TOGETHER, or the merge and non-merge paths silently diverge.
uniform sampler2D envTexture;
uniform sampler2D parallaxTexture;

in vec2 fragTexCoord;

out vec4 fragColor;

void main() {
  vec4 E = texture(envTexture, fragTexCoord);
  vec4 P = texture(parallaxTexture, fragTexCoord);
  fragColor = vec4(P.rgb + E.rgb * (1.0 - P.a), 1.0);
}
