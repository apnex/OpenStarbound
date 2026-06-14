#version 140

// Slice-3 GPU point lighting: ONE point light's per-cell contribution, replicating
// pointLightingReference + obstacleRaycastDDA (StarCellularLightArray.cpp) cell-for-cell so it
// matches the CPU point pass (the de-risk locked the portable DDA at 0.0011/255). Output =
// subtract(value, attenuation); blended onto the spread accumulation by the caller (additive for
// pointAdditive, GL_MAX otherwise). Output 0 when the cell gets no contribution (additive +0 / max
// no-op). One draw per light over the light's bbox quad.
uniform sampler2D obstacle;     // R8, obstacle in R (1.0 obstacle / 0 air), texel (x,y) = cell (x,y)
uniform vec2 lightStateSize;    // grid dims (w,h)
uniform vec2 lightPosition;     // array-relative grid coords
uniform vec3 lightValue;
uniform float lightBeam;
uniform float lightBeamAngle;
uniform float lightBeamAmbience;
uniform bool lightAsSpread;
uniform float pointMaxAir;
uniform float pointMaxObstacle;
uniform float spreadMaxAir;
uniform float spreadMaxObstacle;
uniform float pointObstacleBoost;

out vec4 fragColor;

const int MAX_DDA_STEPS = 64;

bool obstacleAt(int x, int y) {
  if (x < 0 || y < 0 || x >= int(lightStateSize.x) || y >= int(lightStateSize.y))
    return false;
  return texelFetch(obstacle, ivec2(x, y), 0).r > 0.5;
}

// Anti-aliased DDA obstacle raycast — verbatim port of obstacleRaycastDDA.
float obstacleRaycastDDA(vec2 start, vec2 end, float perObstacleAttenuation, float maxAttenuation) {
  float att = 0.0;
  float x1 = start.x - 0.5, y1 = start.y - 0.5;
  float x2 = end.x - 0.5,   y2 = end.y - 0.5;
  float dx = x2 - x1, dy = y2 - y1;

  if (abs(dx) >= abs(dy)) {
    float gradient = (dx == 0.0) ? 0.0 : dy / dx;
    if (x2 < x1) { float t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    int xStart = int(floor(x1 + 0.5)), xEnd = int(floor(x2 + 0.5));   // round()
    float gapFirst = 1.0 - fract(x1 + 0.5);
    float gapLast = fract(x2 + 0.5);
    float intery = y1 + gradient * (float(xStart) - x1);
    for (int i = 0; i < MAX_DDA_STEPS; ++i) {
      int x = xStart + i;
      if (x > xEnd) break;
      float coverage = (x == xStart) ? gapFirst : (x == xEnd) ? gapLast : 1.0;
      int iy = int(floor(intery));
      float fy = intery - float(iy);
      if (obstacleAt(x, iy))     att += (1.0 - fy) * coverage * perObstacleAttenuation;
      if (obstacleAt(x, iy + 1)) att += fy * coverage * perObstacleAttenuation;
      if (att >= maxAttenuation) return maxAttenuation;
      intery += gradient;
    }
  } else {
    float gradient = (dy == 0.0) ? 0.0 : dx / dy;
    if (y2 < y1) { float t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    int yStart = int(floor(y1 + 0.5)), yEnd = int(floor(y2 + 0.5));
    float gapFirst = 1.0 - fract(y1 + 0.5);
    float gapLast = fract(y2 + 0.5);
    float interx = x1 + gradient * (float(yStart) - y1);
    for (int i = 0; i < MAX_DDA_STEPS; ++i) {
      int y = yStart + i;
      if (y > yEnd) break;
      float coverage = (y == yStart) ? gapFirst : (y == yEnd) ? gapLast : 1.0;
      int ix = int(floor(interx));
      float fx = interx - float(ix);
      if (obstacleAt(ix, y))     att += (1.0 - fx) * coverage * perObstacleAttenuation;
      if (obstacleAt(ix + 1, y)) att += fx * coverage * perObstacleAttenuation;
      if (att >= maxAttenuation) return maxAttenuation;
      interx += gradient;
    }
  }
  return min(att, maxAttenuation);
}

// ColoredLightTraits::subtract — hue-preserving proportional drop.
vec3 coloredSubtract(vec3 c, float drop) {
  float m = max(c.r, max(c.g, c.b));
  if (m <= 0.0) return c;
  return max(c - drop * c / m, vec3(0.0));
}

void main() {
  vec2 blockPos = gl_FragCoord.xy;   // FBO pixel center = cell (x,y) center = (x+0.5, y+0.5)
  float maxIntensity = max(lightValue.r, max(lightValue.g, lightValue.b));
  vec2 beamDirection = vec2(cos(lightBeamAngle), sin(lightBeamAngle));
  float perBlockObstacleAtten = lightAsSpread ? 1.0 / spreadMaxObstacle : 1.0 / pointMaxObstacle;
  float perBlockAirAtten = lightAsSpread ? 1.0 / spreadMaxAir : 1.0 / pointMaxAir;

  vec2 relative = blockPos - lightPosition;
  float distance = length(relative);
  if (distance == 0.0) { fragColor = vec4(lightValue, 1.0); return; }   // light on this cell

  float attenuation = distance * perBlockAirAtten;
  if (attenuation >= 1.0) { fragColor = vec4(0.0); return; }

  vec2 direction = relative / distance;
  if (lightBeam > 0.0) {
    attenuation += (1.0 - lightBeamAmbience) * clamp(lightBeam * (1.0 - dot(direction, beamDirection)), 0.0, 1.0);
    if (attenuation >= 1.0) { fragColor = vec4(0.0); return; }
  }

  float remainingAttenuation = maxIntensity - attenuation;
  if (remainingAttenuation <= 0.0) { fragColor = vec4(0.0); return; }

  float circObsAtten = perBlockObstacleAtten / max(abs(direction.x), abs(direction.y));
  float blockAttenuation = obstacleRaycastDDA(blockPos, lightPosition, circObsAtten, remainingAttenuation);
  attenuation += blockAttenuation;
  if (!lightAsSpread)
    attenuation += min(blockAttenuation, circObsAtten) * pointObstacleBoost;

  if (attenuation >= 1.0) { fragColor = vec4(0.0); return; }

  vec3 newLight = coloredSubtract(lightValue, attenuation);
  if (lightAsSpread) newLight *= 0.15;
  // Additive path gates on maxIntensity(newLight) > 0.0001; for max-blend the same near-zero
  // output is a harmless no-op, so a single guard suffices.
  if (max(newLight.r, max(newLight.g, newLight.b)) <= 0.0001) { fragColor = vec4(0.0); return; }
  fragColor = vec4(newLight, 1.0);
}
