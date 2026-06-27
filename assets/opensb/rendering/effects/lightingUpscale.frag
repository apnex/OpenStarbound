#version 140

// R-A Form 2: bicubic-reconstruct the tile-res composed lightmap into a higher-res (Nx) linear
// target ONCE per lightmap update (<=30Hz). The world pass then does a single bilinear tap of this
// smooth result instead of a 4-tap bicubic per screen fragment at 60Hz. cubic/bicubicSample are
// copied verbatim from world.frag so the upscaled map matches the original bicubic exactly.

uniform sampler2D inputTexture;
uniform vec2 inputTextureSize;

in vec2 fragTexCoord;

out vec4 fragColor;

vec4 cubic(float v) {
  vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
  vec4 s = n * n * n;
  float x = s.x;
  float y = s.y - 4.0 * s.x;
  float z = s.z - 4.0 * s.y + 6.0 * s.x;
  float w = 6.0 - x - y - z;
  return vec4(x, y, z, w);
}

vec4 bicubicSample(sampler2D tex, vec2 texcoord, vec2 texscale) {
  texcoord = texcoord - vec2(0.5, 0.5);

  float fx = fract(texcoord.x);
  float fy = fract(texcoord.y);
  texcoord.x -= fx;
  texcoord.y -= fy;

  vec4 xcubic = cubic(fx);
  vec4 ycubic = cubic(fy);

  vec4 c = vec4(texcoord.x - 0.5, texcoord.x + 1.5, texcoord.y - 0.5, texcoord.y + 1.5);
  vec4 s = vec4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
  vec4 offset = c + vec4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

  vec4 sample0 = texture(tex, vec2(offset.x, offset.z) * texscale);
  vec4 sample1 = texture(tex, vec2(offset.y, offset.z) * texscale);
  vec4 sample2 = texture(tex, vec2(offset.x, offset.w) * texscale);
  vec4 sample3 = texture(tex, vec2(offset.y, offset.w) * texscale);

  float sx = s.x / (s.x + s.y);
  float sy = s.z / (s.z + s.w);

  return mix(
    mix(sample3, sample2, sx),
    mix(sample1, sample0, sx), sy);
}

void main() {
  // fragTexCoord is normalised [0,1] over the (large) target; convert to input texel space and
  // bicubic-reconstruct the low-res composed lightmap.
  vec2 texel = fragTexCoord * inputTextureSize;
  fragColor = vec4(bicubicSample(inputTexture, texel, 1.0 / inputTextureSize).rgb, 1.0);
}
