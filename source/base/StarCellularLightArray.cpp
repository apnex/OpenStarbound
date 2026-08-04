#include "StarCellularLightArray.hpp"
#include "StarInterpolation.hpp"
// just specializing these in a cpp file so I can iterate on them without recompiling like 40 files!!

namespace Star {

// ONE BODY, TWO INSTANTIATIONS. These were two hand-maintained copies 70 lines each; normalising the
// traits name showed them differing in exactly two lines -- one of those a stray indent, the other
// the beam guard below. Two copies of a formula this long is how the two drift apart silently, and
// the point pass is not a place we can afford that.
//
// It stays defined in this TU rather than moving to the header for the reason the original note at
// the top gives: keeping it here means iterating on the math does not recompile every consumer. The
// explicit instantiations at the end preserve that.
template <typename LightTraits>
void CellularLightArray<LightTraits>::calculatePointLighting(size_t xmin, size_t ymin, size_t xmax, size_t ymax) {
  float pointPerBlockObstacleAttenuation = 1.0f / m_pointMaxObstacle;
  float pointPerBlockAirAttenuation = 1.0f / m_pointMaxAir;

  for (PointLight light : m_pointLights) {
    if (light.position[0] < 0 || light.position[0] > m_width - 1 || light.position[1] < 0 || light.position[1] > m_height - 1)
      continue;

    float maxIntensity = LightTraits::maxIntensity(light.value);
    Vec2F beamDirection = Vec2F(1, 0).rotate(light.beamAngle);
    float perBlockObstacleAttenuation = light.asSpread ? 1.0f / m_spreadMaxObstacle : pointPerBlockObstacleAttenuation;
    float perBlockAirAttenuation = light.asSpread ? 1.0f / m_spreadMaxAir : pointPerBlockAirAttenuation;

    float maxRange = maxIntensity * (light.asSpread ? m_spreadMaxAir : m_pointMaxAir);
    // The min / max considering the radius of the light
    size_t lxmin = std::floor(std::max<float>(xmin, light.position[0] - maxRange));
    size_t lymin = std::floor(std::max<float>(ymin, light.position[1] - maxRange));
    size_t lxmax = std::ceil(std::min<float>(xmax, light.position[0] + maxRange));
    size_t lymax = std::ceil(std::min<float>(ymax, light.position[1] + maxRange));

    for (size_t x = lxmin; x < lxmax; ++x) {
      for (size_t y = lymin; y < lymax; ++y) {
        LightValue lvalue = getLight(x, y);
        // + 0.5f to correct block position to center
        Vec2F blockPos = Vec2F(x + 0.5f, y + 0.5f);

        Vec2F relativeLightPosition = blockPos - light.position;
        float distance = relativeLightPosition.magnitude();
        if (distance == 0.0f) {
          setLight(x, y, light.value + lvalue);
          continue;
        }

        float attenuation = distance * perBlockAirAttenuation;
        if (attenuation >= 1.0f)
          continue;

        Vec2F direction = relativeLightPosition / distance;
        // THE ONE SEMANTIC DIFFERENCE between the two instantiations, PRESERVED rather than resolved.
        // Scalar has guarded this with 1e-4 and Colored with 0 since they were separate copies. Both
        // are live -- CellularLightingCalculator's monochrome flag picks one -- so collapsing to
        // either value changes pixels for the other, which makes it an output change and not part of
        // a byte-identical merge. Carried as a traits constant so the divergence is declared in one
        // place instead of hiding in a duplicated body.
        if (light.beam > LightTraits::PointBeamThreshold) {
          attenuation += (1.0f - light.beamAmbience) * clamp(light.beam * (1.0f - direction * beamDirection), 0.0f, 1.0f);
          if (attenuation >= 1.0f)
            continue;
        }

        float remainingAttenuation = maxIntensity - attenuation;
        if (remainingAttenuation <= 0.0f)
          continue;

        // Need to circularize manhattan attenuation here
        float circularizedPerBlockObstacleAttenuation = perBlockObstacleAttenuation / max(fabs(direction[0]), fabs(direction[1]));
        float blockAttenuation = lineAttenuation(blockPos, light.position, circularizedPerBlockObstacleAttenuation, remainingAttenuation);

        attenuation += blockAttenuation;
        // Apply single obstacle boost (determine single obstacle by one
        // block unit of attenuation).
        if (!light.asSpread)
          attenuation += min(blockAttenuation, circularizedPerBlockObstacleAttenuation) * m_pointObstacleBoost;

        if (attenuation < 1.0f) {
          if (m_pointAdditive) {
            auto newLight = LightTraits::subtract(light.value, attenuation);
            if (LightTraits::maxIntensity(newLight) > 0.0001f)
              setLight(x, y, lvalue + (light.asSpread ? newLight * 0.15f : newLight));
          } else {
            setLight(x, y, LightTraits::max(LightTraits::subtract(light.value, attenuation), lvalue));
          }
        }
      }
    }
  }
}

// Explicit, because the definition deliberately stays in this TU. Without these the two
// instantiations would have no home and every consumer would need the body in the header --
// which is the recompile cost the note at the top of this file exists to avoid.
template void CellularLightArray<ScalarLightTraits>::calculatePointLighting(size_t, size_t, size_t, size_t);
template void CellularLightArray<ColoredLightTraits>::calculatePointLighting(size_t, size_t, size_t, size_t);


namespace {
  // Obstacle lookup over the array column-major (x * height + y) obstacle grid.
  // Out-of-bounds cells are treated as air. Production lineAttenuation indexes
  // cell(x,y) with no bounds check (it is protected by the calc-region border +
  // the query-clamped bbox); the references below run over the FULL grid bbox so
  // they bounds-check -- for query cells the ray stays in bounds, so the result
  // is identical to production there.
  inline bool obstacleAt(List<uint8_t> const& obstacle, size_t width, size_t height, int x, int y) {
    if (x < 0 || y < 0 || x >= (int)width || y >= (int)height)
      return false;
    return obstacle[(size_t)x * height + (size_t)y] != 0;
  }

  // Verbatim mirror of CellularLightArray::lineAttenuation (Xiaolin-Wu anti-
  // aliased line, both endpoints -0.5 corner-corrected, major axis = larger of
  // |dx|,|dy|, 2 straddling cells weighted by fpart/rfpart, early-exit at
  // maxAttenuation) reading the obstacle grid instead of the cell array.
  float obstacleLineAttenuation(List<uint8_t> const& obstacle, size_t width, size_t height,
      Vec2F const& start, Vec2F const& end, float perObstacleAttenuation, float maxAttenuation) {
    float obstacleAttenuation = 0.0;

    float x1 = start[0] - 0.5;
    float y1 = start[1] - 0.5;
    float x2 = end[0] - 0.5;
    float y2 = end[1] - 0.5;

    float dx = x2 - x1;
    float dy = y2 - y1;

    if (fabs(dx) < fabs(dy)) {
      if (y2 < y1) {
        swap(y1, y2);
        swap(x1, x2);
      }

      float gradient = dx / dy;

      float yend = round(y1);
      float xend = x1 + gradient * (yend - y1);
      float ygap = rfpart(y1 + 0.5);
      int ypxl1 = yend;
      int xpxl1 = ipart(xend);

      if (obstacleAt(obstacle, width, height, xpxl1, ypxl1))
        obstacleAttenuation += rfpart(xend) * ygap * perObstacleAttenuation;
      if (obstacleAt(obstacle, width, height, xpxl1 + 1, ypxl1))
        obstacleAttenuation += fpart(xend) * ygap * perObstacleAttenuation;

      if (obstacleAttenuation >= maxAttenuation)
        return maxAttenuation;

      float interx = xend + gradient;

      yend = round(y2);
      xend = x2 + gradient * (yend - y2);
      ygap = fpart(y2 + 0.5);
      int ypxl2 = yend;
      int xpxl2 = ipart(xend);

      if (obstacleAt(obstacle, width, height, xpxl2, ypxl2))
        obstacleAttenuation += rfpart(xend) * ygap * perObstacleAttenuation;
      if (obstacleAt(obstacle, width, height, xpxl2 + 1, ypxl2))
        obstacleAttenuation += fpart(xend) * ygap * perObstacleAttenuation;

      if (obstacleAttenuation >= maxAttenuation)
        return maxAttenuation;

      for (int y = ypxl1 + 1; y < ypxl2; ++y) {
        int interxIpart = ipart(interx);
        float interxFpart = interx - interxIpart;
        float interxRFpart = 1.0 - interxFpart;

        if (obstacleAt(obstacle, width, height, interxIpart, y))
          obstacleAttenuation += interxRFpart * perObstacleAttenuation;
        if (obstacleAt(obstacle, width, height, interxIpart + 1, y))
          obstacleAttenuation += interxFpart * perObstacleAttenuation;

        if (obstacleAttenuation >= maxAttenuation)
          return maxAttenuation;

        interx += gradient;
      }
    } else {
      if (x2 < x1) {
        swap(x1, x2);
        swap(y1, y2);
      }

      float gradient = dy / dx;

      float xend = round(x1);
      float yend = y1 + gradient * (xend - x1);
      float xgap = rfpart(x1 + 0.5);
      int xpxl1 = xend;
      int ypxl1 = ipart(yend);

      if (obstacleAt(obstacle, width, height, xpxl1, ypxl1))
        obstacleAttenuation += rfpart(yend) * xgap * perObstacleAttenuation;
      if (obstacleAt(obstacle, width, height, xpxl1, ypxl1 + 1))
        obstacleAttenuation += fpart(yend) * xgap * perObstacleAttenuation;

      if (obstacleAttenuation >= maxAttenuation)
        return maxAttenuation;

      float intery = yend + gradient;

      xend = round(x2);
      yend = y2 + gradient * (xend - x2);
      xgap = fpart(x2 + 0.5);
      int xpxl2 = xend;
      int ypxl2 = ipart(yend);

      if (obstacleAt(obstacle, width, height, xpxl2, ypxl2))
        obstacleAttenuation += rfpart(yend) * xgap * perObstacleAttenuation;
      if (obstacleAt(obstacle, width, height, xpxl2, ypxl2 + 1))
        obstacleAttenuation += fpart(yend) * xgap * perObstacleAttenuation;

      if (obstacleAttenuation >= maxAttenuation)
        return maxAttenuation;

      for (int x = xpxl1 + 1; x < xpxl2; ++x) {
        int interyIpart = ipart(intery);
        float interyFpart = intery - interyIpart;
        float interyRFpart = 1.0 - interyFpart;

        if (obstacleAt(obstacle, width, height, x, interyIpart))
          obstacleAttenuation += interyRFpart * perObstacleAttenuation;
        if (obstacleAt(obstacle, width, height, x, interyIpart + 1))
          obstacleAttenuation += interyFpart * perObstacleAttenuation;

        if (obstacleAttenuation >= maxAttenuation)
          return maxAttenuation;

        intery += gradient;
      }
    }

    return min(obstacleAttenuation, maxAttenuation);
  }

  // GLSL-portable obstacle raycast: the single-loop, shader-idiomatic form of the
  // anti-aliased line. From start to end (both -0.5 corner-corrected, exactly like
  // lineAttenuation), step one cell at a time along the MAJOR axis from the rounded
  // start cell to the rounded end cell INCLUSIVE; at each step sample the two
  // straddling MINOR cells weighted by fpart/rfpart of the minor-axis intercept,
  // and multiply by a per-step COVERAGE that is 1.0 in the interior and the
  // fractional endpoint gap (rfpart(start+0.5) / fpart(end+0.5)) at the two ends --
  // identical anti-aliasing to Xiaolin-Wu, just folded into one uniform loop. The
  // early-exit returns maxAttenuation as soon as the running sum reaches it. No
  // recursion; loop bound = major-axis cell distance (<= maxRange). This is the
  // form the fragment shader will run.
  //
  // The endpoint coverage is LOAD-BEARING: a simpler march that gives the two
  // endpoint cells full straddle weight (no coverage) diverges from production by
  // up to ~26/255 at obstacle cells (a wall cell self-shadowing its own point
  // light, where the integer-aligned start gap = 0.5 is dropped). With the
  // coverage term the form matches production within perceptual tolerance.
  float obstacleRaycastDDA(List<uint8_t> const& obstacle, size_t width, size_t height,
      Vec2F const& start, Vec2F const& end, float perObstacleAttenuation, float maxAttenuation) {
    float obstacleAttenuation = 0.0f;

    float x1 = start[0] - 0.5f;
    float y1 = start[1] - 0.5f;
    float x2 = end[0] - 0.5f;
    float y2 = end[1] - 0.5f;

    float dx = x2 - x1;
    float dy = y2 - y1;

    if (fabs(dx) >= fabs(dy)) {
      // x-major. Order low->high x (obstacle accumulation is commutative); the
      // slope dy/dx is invariant under the swap.
      float gradient = (dx == 0.0f) ? 0.0f : dy / dx;
      if (x2 < x1) {
        swap(x1, x2);
        swap(y1, y2);
      }
      int xStart = (int)round(x1);
      int xEnd = (int)round(x2);
      float gapFirst = rfpart(x1 + 0.5f);
      float gapLast = fpart(x2 + 0.5f);
      float intery = y1 + gradient * ((float)xStart - x1);
      for (int x = xStart; x <= xEnd; ++x) {
        float coverage = (x == xStart) ? gapFirst : (x == xEnd) ? gapLast : 1.0f;
        int iy = ipart(intery);
        float fy = intery - (float)iy;
        float rfy = 1.0f - fy;
        if (obstacleAt(obstacle, width, height, x, iy))
          obstacleAttenuation += rfy * coverage * perObstacleAttenuation;
        if (obstacleAt(obstacle, width, height, x, iy + 1))
          obstacleAttenuation += fy * coverage * perObstacleAttenuation;
        if (obstacleAttenuation >= maxAttenuation)
          return maxAttenuation;
        intery += gradient;
      }
    } else {
      // y-major.
      float gradient = (dy == 0.0f) ? 0.0f : dx / dy;
      if (y2 < y1) {
        swap(x1, x2);
        swap(y1, y2);
      }
      int yStart = (int)round(y1);
      int yEnd = (int)round(y2);
      float gapFirst = rfpart(y1 + 0.5f);
      float gapLast = fpart(y2 + 0.5f);
      float interx = x1 + gradient * ((float)yStart - y1);
      for (int y = yStart; y <= yEnd; ++y) {
        float coverage = (y == yStart) ? gapFirst : (y == yEnd) ? gapLast : 1.0f;
        int ix = ipart(interx);
        float fx = interx - (float)ix;
        float rfx = 1.0f - fx;
        if (obstacleAt(obstacle, width, height, ix, y))
          obstacleAttenuation += rfx * coverage * perObstacleAttenuation;
        if (obstacleAt(obstacle, width, height, ix + 1, y))
          obstacleAttenuation += fx * coverage * perObstacleAttenuation;
        if (obstacleAttenuation >= maxAttenuation)
          return maxAttenuation;
        interx += gradient;
      }
    }

    return min(obstacleAttenuation, maxAttenuation);
  }
}

List<Vec3F> pointLightingReference(List<Vec3F> const& base, List<uint8_t> const& obstacle,
    List<ColoredCellularLightArray::PointLight> const& lights, size_t width, size_t height,
    PointParameters const& params, ObstacleRaycast raycast) {
  starAssert(base.size() == width * height && obstacle.size() == width * height);

  // Accumulate point lights on top of the spread-only base, sequentially in the
  // given order -- exactly as production calculatePointLighting reads getLight
  // (spread + prior point lights at the same cell) and writes setLight. Each
  // light visits each bbox cell at most once, so per-cell order matches; the per-
  // cell math reads only that cell's accumulated light + the obstacle grid, so
  // running the full-grid bbox (rather than production's query-clamped bbox) is
  // identical over the query region.
  List<Vec3F> result = base;

  float pointPerBlockObstacleAttenuation = 1.0f / params.pointMaxObstacle;
  float pointPerBlockAirAttenuation = 1.0f / params.pointMaxAir;

  for (ColoredCellularLightArray::PointLight light : lights) {
    if (light.position[0] < 0 || light.position[0] > width - 1 || light.position[1] < 0 || light.position[1] > height - 1)
      continue;

    float maxIntensity = ColoredLightTraits::maxIntensity(light.value);
    Vec2F beamDirection = Vec2F(1, 0).rotate(light.beamAngle);
    float perBlockObstacleAttenuation = light.asSpread ? 1.0f / params.spreadMaxObstacle : pointPerBlockObstacleAttenuation;
    float perBlockAirAttenuation = light.asSpread ? 1.0f / params.spreadMaxAir : pointPerBlockAirAttenuation;

    float maxRange = maxIntensity * (light.asSpread ? params.spreadMaxAir : params.pointMaxAir);
    size_t lxmin = std::floor(std::max<float>(0.0f, light.position[0] - maxRange));
    size_t lymin = std::floor(std::max<float>(0.0f, light.position[1] - maxRange));
    size_t lxmax = std::ceil(std::min<float>((float)width, light.position[0] + maxRange));
    size_t lymax = std::ceil(std::min<float>((float)height, light.position[1] + maxRange));

    for (size_t x = lxmin; x < lxmax; ++x) {
      for (size_t y = lymin; y < lymax; ++y) {
        size_t index = x * height + y;
        Vec3F lvalue = result[index];
        // + 0.5f to correct block position to center
        Vec2F blockPos = Vec2F(x + 0.5f, y + 0.5f);

        Vec2F relativeLightPosition = blockPos - light.position;
        float distance = relativeLightPosition.magnitude();
        if (distance == 0.0f) {
          result[index] = light.value + lvalue;
          continue;
        }

        float attenuation = distance * perBlockAirAttenuation;
        if (attenuation >= 1.0f)
          continue;

        Vec2F direction = relativeLightPosition / distance;
        if (light.beam > 0.0f) {
          attenuation += (1.0f - light.beamAmbience) * clamp(light.beam * (1.0f - direction * beamDirection), 0.0f, 1.0f);
          if (attenuation >= 1.0f)
            continue;
        }

        float remainingAttenuation = maxIntensity - attenuation;
        if (remainingAttenuation <= 0.0f)
          continue;

        // Need to circularize manhattan attenuation here
        float circularizedPerBlockObstacleAttenuation = perBlockObstacleAttenuation / max(fabs(direction[0]), fabs(direction[1]));
        float blockAttenuation = raycast == ObstacleRaycast::PortableDDA
            ? obstacleRaycastDDA(obstacle, width, height, blockPos, light.position, circularizedPerBlockObstacleAttenuation, remainingAttenuation)
            : obstacleLineAttenuation(obstacle, width, height, blockPos, light.position, circularizedPerBlockObstacleAttenuation, remainingAttenuation);

        attenuation += blockAttenuation;
        // Apply single obstacle boost (determine single obstacle by one
        // block unit of attenuation).
        if (!light.asSpread)
          attenuation += min(blockAttenuation, circularizedPerBlockObstacleAttenuation) * params.pointObstacleBoost;

        if (attenuation < 1.0f) {
          if (params.pointAdditive) {
            auto newLight = ColoredLightTraits::subtract(light.value, attenuation);
            if (ColoredLightTraits::maxIntensity(newLight) > 0.0001f)
              result[index] = lvalue + (light.asSpread ? newLight * 0.15f : newLight);
          } else {
            result[index] = ColoredLightTraits::max(ColoredLightTraits::subtract(light.value, attenuation), lvalue);
          }
        }
      }
    }
  }

  return result;
}

List<Vec3F> spreadJacobiReference(List<Vec3F> const& emission, List<uint8_t> const& obstacle,
    size_t width, size_t height, SpreadParameters const& params, unsigned iterations) {
  starAssert(emission.size() == width * height && obstacle.size() == width * height);

  float dropoffAir = 1.0f / params.spreadMaxAir;
  float dropoffObstacle = 1.0f / params.spreadMaxObstacle;

  // 8-connected neighbour offsets; index layout is x * height + y to match
  // CellularLightArray::cell.
  static int const offsets[8][2] = {
    {-1, -1}, {-1, 0}, {-1, 1},
    { 0, -1},          { 0, 1},
    { 1, -1}, { 1, 0}, { 1, 1}
  };

  // Ping-pong two buffers via raw pointers (List::swap is an element-by-index
  // swap, not a buffer swap, so std::swap on the pointers is what we want).
  List<Vec3F> bufferA = emission;
  List<Vec3F> bufferB;
  bufferB.resize(emission.size());
  List<Vec3F>* in = &bufferA;
  List<Vec3F>* out = &bufferB;

  for (unsigned iter = 0; iter < iterations; ++iter) {
    for (size_t x = 0; x < width; ++x) {
      for (size_t y = 0; y < height; ++y) {
        size_t t = x * height + y;
        // Base each cell on its own emission, then accumulate (per-channel max,
        // via spread) the contribution of each neighbour acting as a SOURCE.
        // This is the parallel analogue of calculateLightSpread, whose dropoff
        // is keyed on the source cell's obstacle flag -> here obstacle[n].
        Vec3F value = emission[t];
        for (auto const& off : offsets) {
          int nx = (int)x + off[0];
          int ny = (int)y + off[1];
          if (nx < 0 || ny < 0 || nx >= (int)width || ny >= (int)height)
            continue;
          size_t n = (size_t)nx * height + (size_t)ny;
          bool diagonal = off[0] != 0 && off[1] != 0;
          float dropoff = obstacle[n] ? dropoffObstacle : dropoffAir;
          if (diagonal)
            dropoff = (float)(dropoff * Constants::sqrt2);
          value = ColoredLightTraits::spread((*in)[n], value, dropoff);
        }
        (*out)[t] = value;
      }
    }
    std::swap(in, out);
  }

  // After the final swap, *in holds the latest iteration. brightnessLimit
  // post-step, matching CellularLightingCalculator::calculate (colored:
  // proportional cap preserving hue).
  List<Vec3F>& result = *in;
  for (size_t i = 0; i < result.size(); ++i) {
    Vec3F light = result[i];
    float intensity = ColoredLightTraits::maxIntensity(light);
    if (intensity > params.brightnessLimit)
      light *= params.brightnessLimit / intensity;
    result[i] = light;
  }

  return result;
}

}