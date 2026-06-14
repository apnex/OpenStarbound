#version 140

// Slice-2 GPU spread: ONE parallel Jacobi relaxation step, replicating spreadJacobiReference
// (StarCellularLightArray.cpp) cell-for-cell so it converges to the CPU Gauss-Seidel sweep.
//   value = emission[T]; for each of the 8 neighbours N (as SOURCE):
//     dropoff = (obstacle[N] ? 1/spreadMaxObstacle : 1/spreadMaxAir) * (diagonal ? sqrt2 : 1)
//     value = coloredSpread(lightState[N], value, dropoff)
// coloredSpread(src,dst,drop): m=max(src.rgb); if m<=0 -> dst; drop/=m; max(src - src*drop, dst).
// Emission is re-injected as the per-cell base every iteration; neighbours are read from the
// previous iteration's light state (the ping-pong buffer). On the final pass applyCap clamps to
// brightnessLimit (proportional, hue-preserving) exactly as CellularLightingCalculator::calculate.
uniform sampler2D emission;     // seeded spread light (constant across iterations), RGB float
uniform sampler2D lightState;   // previous iteration's light (== emission on iteration 0)
uniform sampler2D obstacle;     // R8, obstacle in R channel (1.0 obstacle / 0 air)
uniform vec2 lightStateSize;    // texel dimensions of the lightmap grid
uniform float dropoffAir;       // 1 / spreadMaxAir
uniform float dropoffObstacle;  // 1 / spreadMaxObstacle
uniform bool applyCap;          // final pass only
uniform float brightnessLimit;

in vec2 fragTexCoord;

out vec4 fragColor;

const float SQRT2 = 1.41421356237;

vec3 coloredSpread(vec3 source, vec3 dest, float drop) {
  float maxChannel = max(source.r, max(source.g, source.b));
  if (maxChannel <= 0.0)
    return dest;
  drop /= maxChannel;
  return max(source - source * drop, dest);
}

void main() {
  vec2 texel = 1.0 / lightStateSize;
  vec3 value = texture(emission, fragTexCoord).rgb;

  // 8-connected neighbours; dropoff keyed on the SOURCE (neighbour) cell's obstacle flag.
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      if (dx == 0 && dy == 0)
        continue;
      vec2 nuv = fragTexCoord + vec2(float(dx), float(dy)) * texel;
      // Out-of-grid neighbours contribute nothing (clamp addressing would re-read the edge; guard).
      if (nuv.x < 0.0 || nuv.y < 0.0 || nuv.x > 1.0 || nuv.y > 1.0)
        continue;
      vec3 src = texture(lightState, nuv).rgb;
      bool obstacleCell = texture(obstacle, nuv).r > 0.5;
      float dropoff = obstacleCell ? dropoffObstacle : dropoffAir;
      if (dx != 0 && dy != 0)
        dropoff *= SQRT2;
      value = coloredSpread(src, value, dropoff);
    }
  }

  if (applyCap) {
    float intensity = max(value.r, max(value.g, value.b));
    if (intensity > brightnessLimit)
      value *= brightnessLimit / intensity;
  }

  fragColor = vec4(value, 1.0);
}
