#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"
#include "StarMathCommon.hpp"
#include "StarLogging.hpp"

#include <cstring>
#include <cstdint>
#include <cmath>

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

void GpuLightmapPass::composeTail(Vec2U size, float w, float h, char const* lastTarget,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights, PointParameters const& params,
    float brightnessScale, bool tonemap, bool shadowCompare, Image* gpuResult) {
  static auto pointLightsDrawn = Telemetry::counter("lighting.gpu.point.lights");

  auto fullQuad = renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f);

  // lightingPoint has its own "obstacle" sampler -> upload (R8, a third the bytes of RGB24).
  auto uploadObstacle = [&]() {
    if (obstacleR8.size() == (size_t)size[0] * size[1])
      m_renderer->setEffectTextureR8("obstacle", size, obstacleR8.ptr());
    else
      m_renderer->setEffectTexture("obstacle", obstacle);
  };

  // --- Point: one blended per-light bbox quad on top of the spread result (in lastTarget). ---
  if (!lights.empty()) {
    m_renderer->switchEffectConfig("lightingPoint");   // flushes the final spread quad into lastTarget
    m_renderer->beginGpuTimer("lighting.gpu.point.gpu_us");
    uploadObstacle();
    m_renderer->setEffectParameter("pointObstacleBoost", params.pointObstacleBoost);
    m_renderer->setRenderTarget(String(lastTarget), size);   // accumulate onto the spread result
    m_renderer->setBlendMode(params.pointAdditive ? BlendMode::Additive : BlendMode::Max);

    unsigned drawn = 0;
    for (auto const& light : lights) {
      // Match production: skip lights whose center is outside the grid.
      if (light.position[0] < 0 || light.position[0] > w - 1 || light.position[1] < 0 || light.position[1] > h - 1)
        continue;
      float maxIntensity = max(light.value[0], max(light.value[1], light.value[2]));
      float airReach = light.asSpread ? params.spreadMaxAir : params.pointMaxAir;
      float obstacleReach = light.asSpread ? params.spreadMaxObstacle : params.pointMaxObstacle;
      // 1B: the shader's air cull hard-caps reach at airReach regardless of intensity, so for
      // maxIntensity>1 the old bbox (maxIntensity*airReach) drew a wide ring of always-output-0
      // cells. Cap the rect to the cull radius -> byte-identical, fill drops ~maxIntensity^2.
      float maxRange = std::min(maxIntensity, 1.0f) * airReach;
      float lxmin = floor(std::max(0.0f, light.position[0] - maxRange));
      float lymin = floor(std::max(0.0f, light.position[1] - maxRange));
      float lxmax = ceil(std::min(w, light.position[0] + maxRange));
      float lymax = ceil(std::min(h, light.position[1] + maxRange));
      if (lxmax <= lxmin || lymax <= lymin)
        continue;
      m_renderer->setEffectParameter("lightPosition", Vec2F(light.position));
      m_renderer->setEffectParameter("lightValue", Vec3F(light.value));
      m_renderer->setEffectParameter("lightBeam", light.beam);
      m_renderer->setEffectParameter("lightAsSpread", light.asSpread);
      // 1A hoist: per-light constants the shader used to recompute per-fragment (cos/sin etc.).
      m_renderer->setEffectParameter("lightMaxIntensity", maxIntensity);
      m_renderer->setEffectParameter("beamDirection", Vec2F(std::cos(light.beamAngle), std::sin(light.beamAngle)));
      m_renderer->setEffectParameter("oneMinusBeamAmbience", 1.0f - light.beamAmbience);
      m_renderer->setEffectParameter("perBlockObstacleAtten", 1.0f / obstacleReach);
      m_renderer->setEffectParameter("perBlockAirAtten", 1.0f / airReach);
      m_renderer->render(renderFlatRect(RectF(lxmin, lymin, lxmax, lymax), Vec4B::filled(255), 0.0f));
      ++drawn;
    }
    m_renderer->endGpuTimer("lighting.gpu.point.gpu_us");
    m_renderer->setBlendMode(BlendMode::Alpha);   // restore before the compose + world draw
    pointLightsDrawn.inc(drawn);
  }

  // --- Compose: cap (brightnessLimit) the spread+point accumulation into the PERSISTENT lightmap. ---
  // dirty-REGION Stage 1: compose into a fixed, parity-independent clear:false target instead of the
  // spreadIterations%2 ping-pong buffer (which alternates frame-to-frame as the auto-scaled iteration
  // count changes its parity). A stable persistent lightmap is the prerequisite for later stages to
  // scissor partial recompute onto the prior frame and reuse it outside the dirty rect. Full recompute
  // overwrites it entirely via the full quad below, so this is byte-identical to the prior behaviour.
  char const* composeTarget = "lightingMapPersist";   // persistL; a distinct buffer from lastTarget
  m_renderer->switchEffectConfig("lightingPassthrough");        // flushes the final point quad
  m_renderer->beginGpuTimer("lighting.gpu.compose.gpu_us");
  m_renderer->setEffectParameter("applyCap", true);
  m_renderer->setEffectParameter("brightnessLimit", params.brightnessLimit);
  m_renderer->setEffectParameter("brightnessScale", brightnessScale);
  m_renderer->setEffectParameter("tonemap", tonemap);
  m_renderer->setEffectTextureFromTarget("inputTexture", lastTarget);
  m_renderer->setRenderTarget(String(composeTarget), size);
  m_renderer->render(fullQuad);
  m_renderer->endGpuTimer("lighting.gpu.compose.gpu_us");
  m_renderer->flush();

  if (shadowCompare && gpuResult)
    *gpuResult = m_renderer->readFrameBuffer(composeTarget);

  m_renderer->setRenderTarget({});
  m_renderer->switchEffectConfig("world");
  m_renderer->setEffectTextureFromTarget("lightMap", composeTarget);
}

bool GpuLightmapPass::processFull(ImageView const& emission, List<uint16_t> const& emissionHalf,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
    PointParameters const& params, float brightnessScale, bool tonemap, bool shadowCompare,
    Image* gpuResult, bool captureSpread) {
  static auto cpuCostTimer = Telemetry::timer("lighting.gpu.cpu_cost.us");
  static auto spreadPasses = Telemetry::counter("lighting.gpu.spread.passes");
  TelemetryScope cpuCostScope(cpuCostTimer);

  // dirty-REGION Stage 2 caveat 4: the output oracle runs the FULL path after the partial path (which
  // sets a scissor) -> defensively clear the scissor on EVERY exit path (incl. the early-returns below)
  // so the full path is never accidentally clipped.
  m_renderer->setScissorRect({});
  Vec2U size = emission.size;
  float w = (float)size[0], h = (float)size[1];
  if (size[0] == 0 || size[1] == 0 || spreadIterations == 0)
    return false;
  if (!m_renderer->switchEffectConfig("lightingSpread"))
    return false;   // assets missing -> caller falls back to CPU

  auto fullQuad = renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f);
  char const* targets[2] = {"lightingGpu", "lightingGpuB"};

  // Upload the obstacle mask to the current effect's "obstacle" sampler as R8 (a third the bytes of
  // RGB24) from the lighting-thread-extracted mask; fall back to RGB24 if absent. Used by BOTH the
  // spread and point effects (each has its own sampler), so this runs once per effect.
  auto uploadObstacle = [&]() {
    if (obstacleR8.size() == (size_t)size[0] * size[1])
      m_renderer->setEffectTextureR8("obstacle", size, obstacleR8.ptr());
    else
      m_renderer->setEffectTexture("obstacle", obstacle);
  };

  // --- Spread: K Jacobi iterations, NO cap (point lighting is blended on top before the cap). ---
  // Upload emission as RGB16F from the lighting-thread-converted half buffer (half the per-frame
  // transfer); fall back to the RGB_F upload if the half buffer is absent/mismatched.
  m_renderer->beginGpuTimer("lighting.gpu.spread.gpu_us");
  if (emissionHalf.size() == (size_t)size[0] * size[1] * 3)
    m_renderer->setEffectTextureHalfRGB("emission", size, emissionHalf.ptr());
  else
    m_renderer->setEffectTexture("emission", emission);
  uploadObstacle();
  m_renderer->setEffectParameter("dropoffAir", 1.0f / params.spreadMaxAir);
  m_renderer->setEffectParameter("dropoffObstacle", 1.0f / params.spreadMaxObstacle);
  m_renderer->setEffectParameter("applyCap", false);

  char const* lastTarget = nullptr;
  for (unsigned i = 0; i < spreadIterations; ++i) {
    char const* target = targets[i % 2];
    m_renderer->setRenderTarget(String(target), size);
    if (i == 0)
      // iteration-0 light state == emission: alias the already-uploaded emission texture into the
      // lightState sampler instead of uploading the same grid a second time (per-frame upload cut).
      m_renderer->setEffectTextureAlias("lightState", "emission");
    else
      m_renderer->setEffectTextureFromTarget("lightState", lastTarget);
    m_renderer->render(fullQuad);
    lastTarget = target;
  }
  spreadPasses.inc(spreadIterations);
  m_renderer->endGpuTimer("lighting.gpu.spread.gpu_us");

  // dirty-REGION Stage 2: capture the pure-spread result (pre-point) into persistS for the NEXT frame's
  // partial boundary. A read-only copy of lastTarget into a SEPARATE buffer via the passthrough effect
  // (applyCap=false, brightnessScale=1, tonemap=false -> a pure .rgb copy; the forced alpha=1.0 is
  // byte-safe, consumers read .rgb). lastTarget is left untouched, so the point/compose below -- and
  // thus the visible output -- are byte-identical to captureSpread=false (the default/production path).
  if (captureSpread) {
    m_renderer->switchEffectConfig("lightingPassthrough");   // flushes the final spread quad into lastTarget
    m_renderer->setEffectParameter("applyCap", false);
    m_renderer->setEffectParameter("brightnessScale", 1.0f);
    m_renderer->setEffectParameter("tonemap", false);
    m_renderer->setEffectTextureFromTarget("inputTexture", lastTarget);
    m_renderer->setRenderTarget(String("lightingSpreadPersist"), size);
    m_renderer->render(fullQuad);
  }

  // --- Point + compose + bind: the shared tail (byte-identical for processFull / processPartial). ---
  composeTail(size, w, h, lastTarget, obstacle, obstacleR8, lights, params, brightnessScale, tonemap, shadowCompare, gpuResult);
  return true;
}

bool GpuLightmapPass::processPartial(ImageView const& emission, List<uint16_t> const& emissionHalf,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
    PointParameters const& params, RectI const& interior, RectI const& ring,
    float brightnessScale, bool tonemap) {
  static auto cpuCostTimer = Telemetry::timer("lighting.gpu.cpu_cost.us");
  static auto spreadPasses = Telemetry::counter("lighting.gpu.spread.passes");
  static auto partialFrames = Telemetry::counter("lighting.gpu.dirtyregion.partial.frames");
  TelemetryScope cpuCostScope(cpuCostTimer);

  Vec2U size = emission.size;
  float w = (float)size[0], h = (float)size[1];
  if (size[0] == 0 || size[1] == 0 || spreadIterations == 0) {
    m_renderer->setScissorRect({});   // caveat 4: never leave a scissor set on any exit path
    return false;
  }
  if (!m_renderer->switchEffectConfig("lightingPassthrough")) {
    m_renderer->setScissorRect({});
    return false;   // assets missing -> caller falls back to CPU
  }

  auto fullQuad = renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f);
  char const* W0 = "lightingGpu";
  char const* W1 = "lightingGpuB";
  char const* persistS = "lightingSpreadPersist";
  RectI fullRect(0, 0, (int)size[0], (int)size[1]);
  // The interior/ring arrive clamped to the calc grid (Hop A + render-side); clamp once more for safety
  // (copy first: Box::limited is non-const, so it cannot be called on the const& params directly).
  RectI interiorS = interior;
  interiorS = interiorS.limited(fullRect);
  RectI ringS = ring;
  ringS = ringS.limited(fullRect);

  m_renderer->beginGpuTimer("lighting.gpu.spread.gpu_us");

  // --- Pre-pass A: persistS -> BOTH W0/W1 over the RING (the +1 halo around the interior). The fixed
  // Dirichlet boundary: the Jacobi writes ONLY the interior (scissor), so the ring stays = persistS in
  // both ping-pong buffers and the converged interior reads the prior frame's pure spread there. ---
  m_renderer->setEffectParameter("applyCap", false);
  m_renderer->setEffectParameter("brightnessScale", 1.0f);
  m_renderer->setEffectParameter("tonemap", false);
  m_renderer->setScissorRect(ringS);
  m_renderer->setEffectTextureFromTarget("inputTexture", persistS);
  m_renderer->setRenderTarget(String(W0), size);
  m_renderer->render(fullQuad);
  m_renderer->setRenderTarget(String(W1), size);
  m_renderer->render(fullQuad);

  // --- Pre-pass B: current emission -> BOTH W0/W1 over the INTERIOR. From-below seed matching the full
  // path's iteration-0 lightState==emission. Subtractive edits (light removed / obstacle added) need
  // the seed from CURRENT emission, NOT the stale cached field, since the max-flood can't descend. ---
  m_renderer->setScissorRect(interiorS);
  if (emissionHalf.size() == (size_t)size[0] * size[1] * 3)
    m_renderer->setEffectTextureHalfRGB("inputTexture", size, emissionHalf.ptr());
  else
    m_renderer->setEffectTexture("inputTexture", emission);
  m_renderer->setRenderTarget(String(W0), size);
  m_renderer->render(fullQuad);
  m_renderer->setRenderTarget(String(W1), size);
  m_renderer->render(fullQuad);

  // --- K Jacobi iterations on the interior (ring held = persistS); ping-pong W0/W1 full-grid quads
  // under the interior scissor. SAME K + SAME operator as the full path -> the same converged
  // (seed-independent) max-fixed-point; the partial's iteration-0 parity is irrelevant once converged. ---
  m_renderer->switchEffectConfig("lightingSpread");
  if (emissionHalf.size() == (size_t)size[0] * size[1] * 3)
    m_renderer->setEffectTextureHalfRGB("emission", size, emissionHalf.ptr());
  else
    m_renderer->setEffectTexture("emission", emission);
  if (obstacleR8.size() == (size_t)size[0] * size[1])
    m_renderer->setEffectTextureR8("obstacle", size, obstacleR8.ptr());
  else
    m_renderer->setEffectTexture("obstacle", obstacle);
  m_renderer->setEffectParameter("dropoffAir", 1.0f / params.spreadMaxAir);
  m_renderer->setEffectParameter("dropoffObstacle", 1.0f / params.spreadMaxObstacle);
  m_renderer->setEffectParameter("applyCap", false);
  m_renderer->setScissorRect(interiorS);

  char const* lastTarget = W0;   // the seeded buffer iteration 0 reads
  for (unsigned i = 0; i < spreadIterations; ++i) {
    char const* inBuf = (i % 2 == 0) ? W0 : W1;
    char const* outBuf = (i % 2 == 0) ? W1 : W0;
    m_renderer->setRenderTarget(String(outBuf), size);
    m_renderer->setEffectTextureFromTarget("lightState", inBuf);
    m_renderer->render(fullQuad);
    lastTarget = outBuf;
  }
  spreadPasses.inc(spreadIterations);
  m_renderer->endGpuTimer("lighting.gpu.spread.gpu_us");

  // --- Writeback: the fresh interior -> persistS (interior scissor). persistS now holds the CURRENT
  // full spread: fresh interior + the unchanged prior spread everywhere outside (the dilation D
  // guarantees the outside is identical to a full recompute). It becomes next frame's boundary source. ---
  m_renderer->switchEffectConfig("lightingPassthrough");
  m_renderer->setEffectParameter("applyCap", false);
  m_renderer->setEffectParameter("brightnessScale", 1.0f);
  m_renderer->setEffectParameter("tonemap", false);
  m_renderer->setScissorRect(interiorS);
  m_renderer->setEffectTextureFromTarget("inputTexture", lastTarget);
  m_renderer->setRenderTarget(String(persistS), size);
  m_renderer->render(fullQuad);

  // --- Materialize the full-grid spread into W0 (NO scissor) so the FULL point+compose tail runs
  // exactly as processFull does (identical bytes, identical iteration count). lastTarget = W0. ---
  m_renderer->setScissorRect({});
  m_renderer->setEffectTextureFromTarget("inputTexture", persistS);
  m_renderer->setRenderTarget(String(W0), size);
  m_renderer->render(fullQuad);
  lastTarget = W0;

  // --- Shared FULL point+compose+bind tail (byte-identical to processFull). ---
  composeTail(size, w, h, lastTarget, obstacle, obstacleR8, lights, params, brightnessScale, tonemap, false, nullptr);
  partialFrames.inc(1);
  return true;
}

bool GpuLightmapPass::processValidateDirtyRegion(ImageView const& emission, List<uint16_t> const& emissionHalf,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
    PointParameters const& params, RectI const& interior, RectI const& ring,
    bool forceFull, bool selfCheck, float brightnessScale, bool tonemap, Image* gpuResult) {
  static auto mismatchCounter = Telemetry::counter("lighting.gpu.dirtyregion.validate.mismatch");
  static auto framesCounter = Telemetry::counter("lighting.gpu.dirtyregion.validate.frames");
  static auto nondeterminismCounter = Telemetry::counter("lighting.gpu.dirtyregion.validate.nondeterminism");
  static uint64_t frames = 0, mismatches = 0;
  static int warnBudget = 8;

  Vec2U size = emission.size;
  if (size[0] == 0 || size[1] == 0 || spreadIterations == 0) {
    m_renderer->setScissorRect({});
    return false;
  }

  // 0. SELF-CHECK GATE (when selfCheck is enabled and not yet passed): the full path is
  // prior-content-independent, so two back-to-back full runs on the same inputs must read back
  // byte-identical. This proves the readback/compare oracle is sound BEFORE any partial-vs-full delta is
  // trusted. Run ONLY the self-check this frame and return -- do NOT run/count the partial (the 2nd full
  // is the visible frame + captures persistS for the next partial). Runs each validate frame until it
  // passes once; thereafter (or if selfCheck is off -- user opted out) the partial dual-run proceeds.
  if (selfCheck && !m_dirtyRegionSelfCheckPassed) {
    m_renderer->setScissorRect({});
    processFull(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, brightnessScale, tonemap, false, nullptr, true);
    Image a = m_renderer->readFrameBuffer("lightingMapPersist");
    bool ok2 = processFull(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, brightnessScale, tonemap, false, nullptr, true);
    Image b = m_renderer->readFrameBuffer("lightingMapPersist");
    size_t bytes = (size_t)a.size()[0] * a.size()[1] * 3 * sizeof(float);
    if (a.size() == b.size() && a.size()[0] > 0 && std::memcmp(a.data(), b.data(), bytes) == 0) {
      m_dirtyRegionSelfCheckPassed = true;
      Logger::info("lighting dirty-REGION SELF-CHECK OK: full-vs-full 0 bytes differ ({}x{}); partial now trusted", a.size()[0], a.size()[1]);
    } else {
      Logger::warn("lighting dirty-REGION SELF-CHECK FAILED: full-vs-full differs -> the readback/compare oracle is unreliable; partial deltas NOT trustworthy");
    }
    if (gpuResult)
      *gpuResult = b;
    return ok2;   // partial NOT run/counted until the self-check has proven the oracle sound
  }

  // 1. Force-full frames are not comparable (the partial path itself would force-full); run the full
  // path (capturing persistS for the next partial frame) + bind, and return -- not counted.
  if (forceFull) {
    m_renderer->setScissorRect({});
    return processFull(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, brightnessScale, tonemap, false, gpuResult, true);
  }

  // 2. PARTIAL: run it, read back the composed lightmap (persistL). The partial's writeback touches only
  // persistS's INTERIOR; its RING (which a second partial reads) is untouched -> partial2 is deterministic.
  if (!processPartial(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, interior, ring, brightnessScale, tonemap)) {
    m_renderer->setScissorRect({});
    return processFull(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, brightnessScale, tonemap, false, gpuResult, true);
  }
  Image partial = m_renderer->readFrameBuffer("lightingMapPersist");

  // 2b. Determinism: a second partial must read back identical to the first (same ring from persistS,
  // interior re-seeded from emission). A nonzero delta here is a partial-path nondeterminism bug.
  processPartial(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, interior, ring, brightnessScale, tonemap);
  Image partial2 = m_renderer->readFrameBuffer("lightingMapPersist");

  // 3. FULL (authoritative, LAST): total-overwrites persistS + persistL and ends with the world bind, so
  // the VISIBLE frame is always the full result (a partial/validate bug can never corrupt the screen).
  // No snapshot/restore needed at Stage 2 (becomes load-bearing at Stage 3 when point goes partial too).
  m_renderer->setScissorRect({});
  bool ok = processFull(emission, emissionHalf, obstacle, obstacleR8, lights, spreadIterations, params, brightnessScale, tonemap, false, nullptr, true);
  Image full = m_renderer->readFrameBuffer("lightingMapPersist");
  if (gpuResult)
    *gpuResult = full;

  // 4. GATE: partial-vs-full, GPU-vs-GPU, exact. readFrameBuffer drops alpha (RGB_F) -> 3 floats/cell.
  // memcmp would mis-flag +-0.0 (bit-differs, value-equal) and could mis-pass equal-bit NaNs, so the
  // headline mismatch count uses FLOAT equality: +-0.0 and NaN-vs-NaN are NOT counted, logged distinctly.
  framesCounter.inc(1);
  ++frames;
  size_t n = (size_t)partial.size()[0] * partial.size()[1] * 3;
  bool comparable = partial.size() == full.size() && partial.size() == partial2.size() && n > 0;
  if (!comparable) {
    if (warnBudget > 0) { --warnBudget;
      Logger::warn("lighting dirty-REGION VALIDATE: readback size mismatch (partial {}x{}, full {}x{}) -> skipped compare",
          partial.size()[0], partial.size()[1], full.size()[0], full.size()[1]);
    }
    return ok;
  }

  float const* pa = (float const*)partial.data();
  float const* pf = (float const*)full.data();
  float const* p2 = (float const*)partial2.data();
  size_t realMismatch = 0, signedZeroArtifacts = 0, detMismatch = 0;
  float worst = 0.0f; size_t worstIdx = 0;
  for (size_t i = 0; i < n; ++i) {
    // partial-vs-partial determinism (float-aware: +-0 / NaN-vs-NaN do not count).
    if (!(p2[i] == pa[i]) && !(std::isnan(p2[i]) && std::isnan(pa[i])))
      ++detMismatch;
    float a = pa[i], b = pf[i];
    uint32_t ba, bb;
    std::memcpy(&ba, &a, 4);
    std::memcpy(&bb, &b, 4);
    bool floatEqual = (a == b) || (std::isnan(a) && std::isnan(b));
    if (floatEqual) {
      if (ba != bb)
        ++signedZeroArtifacts;   // bit-differs but value-equal: a +-0.0 (or NaN-payload) artifact
      continue;
    }
    ++realMismatch;
    float d = std::fabs(a - b);
    if (d > worst) { worst = d; worstIdx = i; }
  }

  if (detMismatch > 0)
    nondeterminismCounter.inc(1);
  if (realMismatch > 0 || detMismatch > 0) {
    if (realMismatch > 0) { mismatchCounter.inc(1); ++mismatches; }
    if (warnBudget > 0) { --warnBudget;
      size_t cell = worstIdx / 3, ch = worstIdx % 3;
      unsigned cx = (unsigned)(cell % (size_t)partial.size()[0]);
      unsigned cy = (unsigned)(cell / (size_t)partial.size()[0]);
      Logger::warn("lighting dirty-REGION VALIDATE MISMATCH: {} real cell-channel deltas (worst {:.5f} at cell ({},{}) ch {} partial={:.5f} full={:.5f}); {} determinism deltas; {} +-0 artifacts; selfCheck={}",
          realMismatch, worst, cx, cy, ch, pa[worstIdx], pf[worstIdx], detMismatch, signedZeroArtifacts,
          m_dirtyRegionSelfCheckPassed ? "passed" : (selfCheck ? "PENDING" : "off"));
    }
  }
  // Heartbeat (Stage-0 style): periodic positive evidence the oracle ran real partial frames.
  if (frames == 1 || frames % 300 == 0)
    Logger::info("lighting dirty-REGION VALIDATE OK: {} partial frames, {} mismatches total ({} +-0 artifacts this frame); selfCheck={}",
        frames, mismatches, signedZeroArtifacts, m_dirtyRegionSelfCheckPassed ? "passed" : (selfCheck ? "PENDING" : "off"));
  return ok;
}

}
