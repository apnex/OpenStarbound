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
        // Was the two copies' ONLY semantic difference: Scalar guarded with 1e-4, Colored with 0.
        // Unified on 0 -- "a beam was configured at all" -- because the 1e-4 form was a copy-paste
        // accident, not a design choice, and it made monochrome and colored lighting disagree.
        //
        // Unifying is bit-identical for real content, not merely close: pointBeam is authored per
        // object and defaults to exactly 0, so a light is either 0 (both guards skip) or a real beam
        // (both apply). Every pointBeam in the vanilla pak and in all installed Workshop mods is
        // >= 0.1, three orders of magnitude above the old epsilon, and the value is read straight
        // from config with no interpolation, so nothing transits the (0, 1e-4] window either. Even a
        // hypothetical asset inside it would shift attenuation by at most 2e-4 -- a twentieth of an
        // 8-bit level, since subtract() is linear.
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
}