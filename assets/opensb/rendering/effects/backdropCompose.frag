#version 150

// CM-1: one full-screen pass replacing the two sequential composites into "main".
//
//   E = envTexture      (env cache; the opaque backdrop -- today's env compose forces alpha=1)
//   P = parallaxTexture  (parallax cache; premultiplied coverage -- today's premultiplied-over compose)
//
// The two sequential composites were:
//     main = E                          (opaque replace onto cleared main)
//     main = P.rgb + main * (1 - P.a)   (premultiplied-over)
// which composes to the single expression below. Blend is OFF for this pass (we write the full
// result); env covers every pixel so no clear is needed. Output alpha is 1 (main is opaque).
uniform sampler2D envTexture;
uniform sampler2D parallaxTexture;

in vec2 fragTexCoord;

out vec4 fragColor;

void main() {
  vec4 E = texture(envTexture, fragTexCoord);
  vec4 P = texture(parallaxTexture, fragTexCoord);
  fragColor = vec4(P.rgb + E.rgb * (1.0 - P.a), 1.0);
}
