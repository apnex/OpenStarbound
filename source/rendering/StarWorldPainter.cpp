#include "StarWorldPainter.hpp"
#include "StarAnimation.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarAssets.hpp"
#include "StarJsonExtra.hpp"
#include "StarTelemetry.hpp"
#include "StarLogging.hpp"  // LogMap (/debug HUD per-pass GPU timings, Rung 0)
#include "StarCellularLightingOracle.hpp"  // spreadJacobiReference + pointLightingReference (parity localizer)

namespace Star {

namespace {
  // FILE SCOPE, AND THAT IS THE FIX. Every one of these used to register inside the function that samples
  // it, and every one of those functions is reached only on a path the pinned harness config switches off:
  // shadowCompareFull needs lightingGpuShadowCompare (false), and the CPU lightmap upload is the fallback
  // taken when the GPU pass does NOT run (lightingGpu is true). A counter that can only appear once it has
  // something to report makes "ran and found nothing" and "never ran" the same reading -- and for the
  // parity counter that reading is the only signal the GPU lightmap has that it still matches the CPU one.
  //
  // Registered at static-init rather than behind an accessor function: an accessor is the same lazy
  // registration one call deeper, and it would still first run when something reached it.
  //
  // lighting.upload.us is worse than merely missing. On a leg where the GPU pass fails transiently the key
  // appears MID-WINDOW, and scripts/telemetry-window.py then differences it against an absent entry -- so
  // its whole process-lifetime total, world load included, is reported as this window's cost. That is a
  // fabricated number, not an absent one, and it is silent by construction.
  auto s_pointMismatch = Telemetry::counter("lighting.gpu.point.mismatch",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  // COMPARED vs UNCOMPARABLE, because a mismatch count of zero cannot say WHY it is zero. `compared` counts
  // the invocations that walked both images; `uncomparable` counts the ones that returned on the
  // size-validity check before the loop -- an oracle that reads GREEN precisely when it could not compare.
  auto s_pointCompared = Telemetry::counter("lighting.gpu.point.compared",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  auto s_pointUncomparable = Telemetry::counter("lighting.gpu.point.uncomparable",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  // Cadence::Call: doubly conditional -- inside `if (lightMapUpdated)` AND only on the CPU-lightMap
  // fallback path. Same defect as lighting.gpu.cpu_cost.us; see the note at its registration.
  auto s_uploadTimer = Telemetry::timer("lighting.upload.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Call, .role = MetricRole::Detail,
               .whole = "lighting.cpu.union.us"});
  // The dim overlay draws only when the world is dimmed, so this GPU key is the same defect one layer out --
  // see the note at the head of StarBackdropPass.cpp. It is not a hypothetical: this key is absent from ALL
  // 27 legs of matrix-20260807-071454, which is why it never showed up as a DIFFERENCE between legs and went
  // unnamed while its two compose siblings were being chased.
  auto s_composePassTimer = Telemetry::timer("render.pass.compose.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail, MetricUnit::Microseconds, MetricClock::GpuTimeline});
  // Its .nested rejection counter -- see the note at the head of StarBackdropPass.cpp.
  auto s_composePassNested = Telemetry::counter("render.pass.compose.gpu_us.nested",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail, MetricUnit::Count, MetricClock::NotApplicable});
  // PRODUCER-SIDE LIGHTING (#171), here for the reason this whole block exists: a WorldPainter exists
  // only while a world is LOADED, so a constructor member would leave the key ABSENT on any capture
  // taken outside one, which a consumer differencing two snapshots cannot tell from "ran and cost
  // nothing".
  //
  // CADENCE::CALL, NOT FRAME, AND THE FIRST READING OF THIS INSTRUMENT IS WHAT CORRECTED IT. render()
  // calls adjustLighting inside `if (lightMapUpdated)`, so it fires on lightmap-publish frames only:
  // the smoke capture recorded 347 samples against 900 frames, matching lighting.gpu.cpu_cost.us's
  // count exactly. Declared Frame, telemetry-window would have scaled the total up by 900/347 = 2.59x
  // and invented cost for 553 frames the pass never ran on -- the identical defect cpu_cost's own
  // registration comment documents having already shipped once (1099 of 1500 frames, 1.36x, 458 us
  // reported against 336 actual).
  auto s_adjustLightingTimer = Telemetry::timer("lighting.produce.adjust.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Call, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "lighting.cpu.union.us"});

  // ------------------------------------------------------------------------------------------------
  // THE PAINTER PARTITION ([#286]). render.world.painter.us is ~39% of cpu.frame.render.us and had NO
  // interior: one bracket around WorldPainter::render and nothing inside it. These seven members tile
  // that function exhaustively and without overlap, so the analyser can report a CLOSURE -- parts
  // against an independently measured whole -- rather than an aggregate that cannot be wrong.
  //
  // THE PREFIX IS `render.painter.`, NOT `render.world.painter.`, AND THAT IS LOAD-BEARING. A gate
  // shaped like drive_sites selects members by prefix; "render.world.painter.us" starts with
  // "render.world.painter." and would select the WHOLE as one of its own parts, which is the defect
  // that made a lighting ratchet count its own total. The parts must be prefix-disjoint from the whole,
  // exactly as lighting.gpu.drive.* is disjoint from lighting.gpu.cpu_cost.us.
  //
  // EVERY MEMBER IS UNCONDITIONAL, DELIBERATELY. telemetry-window's window() moves a zero-COUNT timer
  // out of the metrics map into `zeroed`, and aggregate_report computes present/missing from the map
  // alone -- so a member that can legitimately not run reads as ABSENT, and the closure prints "the
  // residual is not interpretable". Every key below therefore fires on every frame (or twice, for the
  // split prologue). Anything conditional -- the chunk REBUILD, the dim overlay -- is a nested probe or
  // stays in the named residual; it is never a member.
  //
  // WALL, NOT ThreadCpu. Several of these regions block: Root and Configuration take process-global
  // mutexes, Assets::image and Assets::json share a mutex with a condvar wait arm, and the renderer
  // flushes. Wall is the honest clock for a region that can wait, and each span hazard is named at its
  // member below rather than left for the reader to rediscover from a surprising number.
  auto s_painterPrologue = Telemetry::timer("render.painter.prologue.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Call, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // Cadence::Call because it records TWICE per frame -- the camera/asset reads at the head, and the
  // BackdropParams config block further down. Declared Frame, telemetry-window would coverage-scale a
  // 2-samples-per-frame key by 2x and invent half of it. SPAN: twelve process-global mutex acquisitions
  // per frame -- Root::loadMemberFunction locks unconditionally, and Configuration::get and
  // getOrDefault each take Configuration's own mutex. Nothing in the tree had ever timed them.
  auto s_painterTiles = Telemetry::timer("render.painter.tiles.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Frame, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // WorldPass::setup -> TilePainter::setup: the WHOLE chunk pipeline -- both chunk hashes (paid on the
  // HIT path, because the hash builds the cache key before the lookup), both TtlCache gets, and the
  // regeneration producers on a miss. This is the only contiguous region that holds the tile work; the
  // four TilePainter::render* calls interleave with entity layers inside renderWorld and only emit
  // primitives already built here. SPAN on a miss: GL vertex upload, atlas allocation, and Assets::image
  // -- and the per-frame resize of the pending-chunk lists runs ~GlRenderBuffer GL deletes in here too.
  auto s_painterEnv = Telemetry::timer("render.painter.env.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Frame, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // BackdropPass::renderEnvironment plus the flush that follows it. IT DOES NOT CONTAIN THE ENV COMPOSE
  // under the shipped or harness config: backdropComposeMerge is true and envRefreshInterval is 4, so
  // the compose is DEFERRED and actually executes inside renderParallax. Reading this key as "the cost
  // of the environment" overstates parallax and understates env by a full-screen compose.
  // The trailing flush is folded in rather than given its own key: with deep telemetry armed every
  // upstream submit point has already drained the immediate list, so a bracket there would time an
  // empty() check -- it is anti-correlated with the only regime that can read it.
  auto s_painterLightmap = Telemetry::timer("render.painter.lightmap.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Frame, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // Both lightmap arms: fullbright, and adjustLighting + runGpuLightmapPass + the binding + the five
  // per-frame effect parameters. A STRICT PARENT of lighting.produce.adjust.us, lighting.upload.us and
  // the whole lighting.gpu.* family -- those declare LIGHTING wholes and must never join this partition,
  // or the same microseconds close two budgets. MetricDesc carries one whole, so that nesting lives in
  // the lint's member lists and in this comment. SPAN: Assets::json shares the assets mutex and its
  // condvar wait arm with Assets::image, and three more Root acquisitions sit in here.
  auto s_painterParallax = Telemetry::timer("render.painter.parallax.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Frame, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // BackdropPass::renderParallax in full -- INCLUDING the env compose renderEnvironment deferred into
  // it. So a standing-to-walking rise here is partly relocated env work: the moving-camera bypass runs
  // an extra standalone compose the parked path does not. Not readable as a parallax cost until the
  // backdrop_compose probe splits it. SPAN: two explicit flushes and three gpuTimer pairs that flush.
  auto s_painterBody = Telemetry::timer("render.painter.body.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Frame, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // WorldPass::renderWorld in full: the interleaved tile / entity / particle / drawable / nametag / bar
  // body. IT IS A MIXTURE BY CONSTRUCTION and cannot be split without splitting renderWorld -- entity
  // layers and tile layers alternate by z-order, so no contiguous span holds either one alone. SPAN,
  // HEAVY: twenty-two renderer flushes are reachable, and three paths reach Assets::getAsset's mutex
  // and its condvar wait -- drawable texture load, the drawable preload arm, and the particle json reads.
  auto s_painterEpilogue = Telemetry::timer("render.painter.epilogue.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Frame, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall,
               .whole = "render.world.painter.us"});
  // The dim compose arm, the /debug HUD loop, and the two cleanup calls -- which run SIX full TtlCache
  // sweeps per frame, each a removeWhere over the entire map, so the cost tracks cache SIZE rather than
  // anything on screen. That shape is a candidate for the part of this bracket that does not move when
  // the player does. SPAN: the HUD loop takes the global LogMap mutex, and the cleanup drops chunk
  // buffers whose destructors call into GL.
}

// GPU-lighting FULL parity shadow-compare (diagnostics only, Slice 3). The GPU result (spread +
// point + cap) is calc-region sized; the CPU lightMap is the full (spread+point+cap) query-region
// result. With point lighting now on BOTH sides this is apples-to-apples in ANY scene. Crop the
// GPU result to the query sub-rect (offset = border on each axis) and compare per channel.
// Tolerance allows RGBA16F + additive float-add order + the DDA-vs-Xiaolin-Wu residual: mean abs
// <= 3/255, max <= 10/255.
static void shadowCompareFull(Image const& gpuCalc, Lightmap const& cpuQuery, int border,
    ImageView const& emission, ImageView const& obstacle,
    List<ColoredCellularLightArray::PointLight> const& lights, PointParameters const& params, unsigned iterations) {
  unsigned qw = cpuQuery.width(), qh = cpuQuery.height();
  Vec2U gpuSize = gpuCalc.size();
  if (qw == 0 || qh == 0 || border < 0 || gpuSize[0] < qw + 2 * border || gpuSize[1] < qh + 2 * border) {
    s_pointUncomparable.inc(1);
    return;
  }
  s_pointCompared.inc(1);

  float const meanTol = 3.0f / 255.0f, maxTol = 10.0f / 255.0f;
  double sumAbs = 0.0;
  float worst = 0.0f;
  unsigned worstX = 0, worstY = 0;
  Vec3F worstCpu, worstGpu;
  float const* gpuData = (float const*)gpuCalc.data();
  for (unsigned y = 0; y < qh; ++y) {
    for (unsigned x = 0; x < qw; ++x) {
      Vec3F cpu = cpuQuery.get(x, y);
      size_t g = ((size_t)(y + border) * gpuSize[0] + (x + border)) * 3;
      Vec3F gpu(gpuData[g], gpuData[g + 1], gpuData[g + 2]);
      for (size_t c = 0; c < 3; ++c) {
        float e = std::fabs(cpu[c] - gpu[c]);
        sumAbs += e;
        if (e > worst) { worst = e; worstX = x; worstY = y; worstCpu = cpu; worstGpu = gpu; }
      }
    }
  }
  float mean = (float)(sumAbs / (qw * qh * 3));
  if (mean > meanTol || worst > maxTol) {
    s_pointMismatch.inc(1);
    static int warnBudget = 8;   // rate-limited; the counter carries the running total
    if (warnBudget > 0) {
      --warnBudget;
      // 3-way localizer: compute the CPU reference (proven spreadJacobiReference + pointLightingReference
      // on the SAME exported inputs) at the worst cell. production==reference but GPU differs => GLSL
      // port bug at this geometry; reference==GPU but production differs => export/reference issue.
      Vec3F ref(-1, -1, -1);
      size_t w = emission.size[0], h = emission.size[1];
      if (w == gpuSize[0] && h == gpuSize[1] && emission.size == obstacle.size) {
        List<Vec3F> em; em.resize(w * h);
        List<uint8_t> ob; ob.resize(w * h);
        float const* eData = (float const*)emission.data;
        uint8_t const* oData = (uint8_t const*)obstacle.data;
        for (size_t x = 0; x < w; ++x)
          for (size_t y = 0; y < h; ++y) {
            size_t px = (y * w + x) * 3;
            em[x * h + y] = Vec3F(eData[px], eData[px + 1], eData[px + 2]);
            ob[x * h + y] = oData[px] > 127 ? 1 : 0;
          }
        auto spread = spreadJacobiReference(em, ob, w, h, SpreadParameters{params.spreadMaxAir, params.spreadMaxObstacle, params.brightnessLimit}, iterations);
        auto full = pointLightingReference(spread, ob, lights, w, h, params);
        ref = full[((size_t)worstX + border) * h + ((size_t)worstY + border)];
      }
      Logger::warn("GPU lighting full parity: mean={:.4f}/255 max={:.4f}/255 at ({},{}) cpu=({:.3f},{:.3f},{:.3f}) gpu=({:.3f},{:.3f},{:.3f}) ref=({:.3f},{:.3f},{:.3f})",
          mean * 255.0f, worst * 255.0f, worstX, worstY,
          worstCpu[0], worstCpu[1], worstCpu[2], worstGpu[0], worstGpu[1], worstGpu[2], ref[0], ref[1], ref[2]);
    }
  }
}

WorldPainter::WorldPainter() {
  m_assets = Root::singleton().assets();

  m_camera.setScreenSize({800, 600});
  m_camera.setCenterWorldPosition(Vec2F());
  m_camera.setPixelRatio(Root::singleton().configuration()->get("zoomLevel").toFloat());

  // The world-body pass owns the tile/drawable/text painters + the entity-render config; it loads that config
  // once in its constructor, exactly where the pre-extraction WorldPainter loaded it. (Its painters are created
  // later, in renderInit -- they are recreated per world entry.)
  //
  // IT IS HANDED THE ASSETS READ ON LINE 81, not left to fetch its own. WorldPainter is the composition root
  // and may know about Root; a pass may not. This is what took WorldPass to zero singleton reads WITHOUT
  // adding one here -- a removal, not the relocation that took BackdropPass from 8 to 0.
  m_worldPass = make_shared<WorldPass>(m_assets);
}

void WorldPainter::renderInit(RendererPtr renderer) {
  m_assets = Root::singleton().assets();

  m_renderer = std::move(renderer);
  m_environmentPainter = make_shared<EnvironmentPainter>(m_renderer);
  // The world-body pass (re)creates its own tile/drawable/text painters here, exactly as the pre-extraction
  // renderInit did -- fresh painters on every world entry.
  m_worldPass->renderInit(m_renderer);
  // renderInit runs again on every world entry / renderer recreation; keep the ONE BackdropPass so its retained
  // env+parallax cache/arbiter state persists across world entries (as the pre-extraction inline members did --
  // reconstructing here would wipe it mid-session). Just refresh its renderer pointer on re-init.
  if (!m_backdropPass)
    m_backdropPass = make_shared<BackdropPass>(m_renderer.get());
  else
    m_backdropPass->setRenderer(m_renderer.get());
  // DELIBERATE improvement (NOT byte-identical, surfaced by step-3 verification): a fresh world / recreated
  // renderer gets fresh backdrop caches, so we never composite the previous world's cached sky on re-entry.
  m_backdropPass->invalidateCaches();
}

void WorldPainter::setCameraPosition(WorldGeometry const& geometry, Vec2F const& position) {
  m_camera.setWorldGeometry(geometry);
  m_camera.setCenterWorldPosition(position);
}

WorldCamera& WorldPainter::camera() {
  return m_camera;
}

void WorldPainter::update(float dt) {
  m_environmentPainter->update(dt);
}

void WorldPainter::pinRayAnimation(uint64_t seed, double timer) {
  m_environmentPainter->pinRayAnimation(seed, timer);
}

LightmapResult WorldPainter::runGpuLightmapPass(WorldRenderData& renderData) {
  auto config = Root::singleton().configuration();
  if (!(config->getOrDefault("lightingGpu").toBool() && renderData.lightingInputsValid))
    return {};   // GPU lighting off / inputs invalid -> caller uses the CPU lightMap
  // Slice 3: compute the COMPLETE lightmap on the GPU (spread + per-light point + cap) from the exported
  // emission/obstacle/point-light grids; processFull restores the world effect + binds the result as lightMap,
  // and returns {active=false} (caller falls back to CPU) if assets are missing.
  if (!m_gpuLightmapPass)
    m_gpuLightmapPass = make_shared<GpuLightmapPass>(m_renderer.get());
  // AIR-GAP CONTRACT (2) FOR THE LIGHTMAP PASS. Every knob resolved once, here at the composition root,
  // and handed over as one value -- the shape BackdropParams established. What used to sit below this line
  // was 45 lines of the PASS'S OWN WORK living in the orchestrator: an O(cells) scan over the pass's first
  // argument, the iteration derivation that consumed it, and five loose trailing arguments. That is the
  // whole reason this pass could show a clean per-file singleton count while the work it needed sat one
  // level up. The scan now lives in the pass; only the config resolution stays here, where it belongs.
  //
  // BUILT AFTER THE ENABLE GATE ON PURPOSE. The /lighting.config JSON read is not free, and must not run on
  // frames where GPU lighting is off -- the original ordering, preserved.
  auto lc = m_assets->json("/lighting.config:lighting");
  LightmapParams params{
      PointParameters{
          lc.getFloat("pointMaxAir"), lc.getFloat("pointMaxObstacle"),
          lc.getFloat("pointObstacleBoost"),
          config->getOrDefault("newLighting").toBool(),   // pointAdditive (matches lightingCalc)
          lc.getFloat("spreadMaxAir"), lc.getFloat("spreadMaxObstacle"),
          lc.getFloat("brightnessLimit")},
      // Explicit cast: toUInt() is wider than unsigned, and a braced init treats the narrowing as an
      // error. BackdropParams hit the identical trap -- the braces are doing their job.
      (unsigned)config->getOrDefault("lightingGpuSpreadIterations").toUInt(),
      config->getOrDefault("lightingGpuShadowCompare").toBool(),
      config->getOrDefault("lightingGpuBrightness").toFloat(),
      config->getOrDefault("lightingTonemap").toBool(),
      config->getOrDefault("lightingWorldUpscale").toFloat()};

  Image gpuResult;
  LightmapResult lm = m_gpuLightmapPass->processFull(renderData.lightingEmission, renderData.lightingEmissionHalf,
      renderData.lightingObstacle, renderData.lightingObstacleR8, renderData.lightingPointLights,
      params, &gpuResult, renderData.lightMapBorder);
  // shadowCompare (diagnostics only): parity-check the GPU result against the CPU reference. It uses the
  // border AND the iteration count the active result carries -- both travel IN the result, so the reference
  // is built with exactly what the pass ran, not with a second derivation that could drift from it.
  //
  // THIS STAYS IN THE ORCHESTRATOR DELIBERATELY. It compares the GPU pass's output against the CPU lighting
  // path's output; neither path owns that comparison, and the orchestrator is the only thing holding both.
  if (lm.active && params.shadowCompare && gpuResult.size()[0] > 0)
    shadowCompareFull(gpuResult, renderData.lightMap, lm.border,
        renderData.lightingEmission, renderData.lightingObstacle, renderData.lightingPointLights,
        params.point, lm.spreadIterations);
  return lm;
}

void WorldPainter::render(WorldRenderData& renderData, function<bool()> lightWaiter) {
  {
    TelemetryScope s(s_painterPrologue);
    m_camera.setScreenSize(m_renderer->screenSize());
    m_camera.setTargetPixelRatio(Root::singleton().configuration()->get("zoomLevel").toFloat());

    m_assets = Root::singleton().assets();
  }

  {
    TelemetryScope s(s_painterTiles);
    m_worldPass->setup(m_camera, renderData, m_assets);
  }

  // Stars, Debris Fields, Sky, and Orbiters

  // RENDER-PASS ABLATION MASK (task #141). The per-pass GL_TIME_ELAPSED timers are NOT ADDITIVE -- with 12 of
  // them their sum overshot the real frame by 5ms, because each bracket serialises the pipeline and measures
  // its own stall. They RANK passes; they cannot BUDGET them. The only honest way to cost a pass is to REMOVE
  // it and re-measure the whole frame. This mask does that, in a LIVE world (a frozen world is required for the
  // byte-identity gate but measures a floor, not real play -- and worse, a frozen+unthrottled world saturates
  // the GPU and manufactures pipeline stalls that do not exist in real play).
  // A SET bit RENDERS its pass; a clear bit ABLATES it. Only the passes listed here exist -- do not document a
  // bit that nothing reads, or setting it measures an un-ablated frame and reports the pass as free, which is
  // precisely the measurement lie this tool was built to prevent.
  //   bit0 env/sky   bit1 parallax
  // Base 0 (NOT 10): the mask is written in hex, and strtol(...,10) parses "0xF" as 0 -- silently ablating every
  // pass, the exact inverse of what was asked for.
  static int const PassAll = 0x3;
  static int const passMask = []() {
    char const* e = getenv("STAR_RENDERTEST_PASS_MASK");
    return e && *e ? (int)strtol(e, nullptr, 0) : PassAll;
  }();
  bool const ablateEnv      = !(passMask & 1);
  bool const ablateParallax = !(passMask & 2);
  // An ablated run must never be mistaken for a normal one -- say so, once.
  if (passMask != PassAll) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      Logger::warn("[rendertest] PASS ABLATION ACTIVE mask={:#x}: env={} parallax={}",
          passMask, ablateEnv ? "ABLATED" : "on", ablateParallax ? "ABLATED" : "on");
    }
  }

  // Sky/environment backdrop: the FBO-generation drop + the env retained cache (N-cadence refresh) + the
  // per-frame compose into "main". renderEnvironment records whether it refreshed, for the parallax arbiter.
  // AIR-GAP CONTRACT (2). Resolve the backdrop pass's config HERE, at the composition root, and hand it a
  // value -- the pass no longer reaches into Root at all. Resolved PER FRAME rather than at construction
  // because every one of these is live-tunable mid-session (`/rendercache envrefresh`, `/rendercache
  // parallaxrefresh`, the antiAliasing option the client polls each frame); freezing them in the
  // constructor, which is what the target-state doc originally prescribed, would have silently broken the
  // console knobs this campaign uses to A/B its own levers.
  //
  // The fallbacks are reproduced EXACTLY as the pass used to read them, including
  // parallaxMaxDriftStepPx's 1.5f -- which never fires, because StarRootLoader ships 0.75. Keeping it
  // makes this move byte-identical rather than byte-identical-plus-one-quiet-change; correcting it is a
  // separate decision, not a refactor's business.
  // DECLARED OUT, ASSIGNED IN. backdropParams is consumed by both renderEnvironment and renderParallax
  // further down, so the timing scope cannot enclose its lifetime -- it encloses only the nine locked
  // reads that build it. Every BackdropParams member is defaulted, so the empty declaration is valid and
  // the value is overwritten whole; nothing reads it between the two statements.
  BackdropParams backdropParams;
  {
    TelemetryScope s(s_painterPrologue);
    auto backdropConfig = Root::singleton().configuration();
    // Every one of these carried TWO hand-typed fallbacks (the get() default and the opt().value()), and
    // parallaxMaxDriftStepPx's said 1.5f against a shipped 0.75. See StarConfiguration::getOrDefault.
    backdropParams = BackdropParams{
        (unsigned)backdropConfig->getOrDefault("envRefreshInterval").toUInt(),
        backdropConfig->getOrDefault("envOracle").toBool(),
        backdropConfig->getOrDefault("envMaxDriftStepPx").toFloat(),
        backdropConfig->getOrDefault("backdropComposeMerge").toBool(),
        backdropConfig->getOrDefault("antiAliasing").toBool(),
        backdropConfig->getOrDefault("parallaxOracle").toBool(),
        (unsigned)backdropConfig->getOrDefault("parallaxRefreshInterval").toUInt(),
        backdropConfig->getOrDefault("parallaxMaxDriftStepPx").toFloat()};
  }

  // THE SLICE. The orchestrator holds the fat struct; the pass gets the two members it actually reads.
  // Built at both call sites rather than hoisted to a local, so the slice is visible where the hand-off
  // happens -- if a third member is ever needed, it has to be added HERE, in the open.
  {
    TelemetryScope s(s_painterEnv);
    m_backdropPass->renderEnvironment(m_camera, {renderData.skyRenderData, renderData.parallaxLayers},
        *m_environmentPainter, backdropParams, ablateEnv);

    m_renderer->flush();
  }

  bool lightMapUpdated = lightWaiter ? lightWaiter() : false;

  {
  TelemetryScope s(s_painterLightmap);
  m_renderer->setEffectParameter("lightMapEnabled", !renderData.isFullbright);
  if (renderData.isFullbright) {
    m_renderer->setEffectTexture("lightMap", Image::filled(Vec2U(1, 1), { 255, 255, 255, 255 }, PixelFormat::RGB24));
    m_renderer->setEffectParameter("lightMapMultiplier", 1.0f);
    // Invariant: m_lightMapBorder always matches the currently-bound lightMap. The 1x1 white
    // texture is offset-invariant so this is defensive, but keeps the border consistent if the
    // binding persists into a later non-fullbright frame before a fresh lighting frame arrives.
    m_lightMapBorder = 0;
    m_lastLightmapFresh = false;
  } else {
    // Assigned at the pass, not beside the border below: the border assignment sits inside both `if`s, so
    // on a frame the pass did not run it would leave the previous frame's result reading as fresh.
    m_lastLightmapFresh = false;
    if (lightMapUpdated) {
      adjustLighting(renderData);
      // GPU-lightmap dispatch is pulled into runGpuLightmapPass(): it prepares the pass inputs and returns an
      // explicit LightmapResult (active + its matching border) instead of a bool plus a separately-read border.
      LightmapResult lm = runGpuLightmapPass(renderData);
      m_lastLightmap = lm;
      m_lastLightmapFresh = lm.active;
      if (lm.active) {
        // The bound lightMap is the calc-region (border-padded) result; the border travels WITH the active
        // flag in the result, so the world shader's lightMapOffset can never pair an active GPU lightMap with a
        // stale/garbage border (it must NOT be reverse-derived from renderData.lightMap width, which is empty
        // => a garbage offset, dark world, when the CPU calc is skipped).
        m_lightMapBorder = lm.border;
      } else if (!renderData.lightMap.empty()) {
        // CPU lightMap upload (deep-gated; also the fallback when the GPU path is off/unavailable). The
        // TIMER stays here, where the cost is; its REGISTRATION does not -- see the file-scope block.
        TelemetryScope uploadScope(s_uploadTimer);
        m_renderer->setEffectTexture("lightMap", renderData.lightMap);
        m_lightMapBorder = 0;
      }
      // else (Slice 4): GPU pass failed AND no CPU lightMap this frame -- the skip-calculate latch raced a
      // transient GPU failure. Keep the previous lightMap binding for this one frame; reporting
      // m_gpuLightingActive=false (below) re-arms the CPU path on the lighting thread, so a real lightMap
      // returns within ~1 frame. (An empty upload would flash black.)
      // Slice 4: report this frame's GPU outcome so the lighting thread can drop the redundant CPU calculate().
      m_gpuLightingActive = lm.active;
    }
    m_renderer->setEffectParameter("lightMapMultiplier", m_assets->json("/rendering.config:lightMapMultiplier").toFloat());
    m_renderer->setEffectParameter("lightMapScale", Vec2F::filled(TilePixels * m_camera.pixelRatio()));
    // m_lightMapBorder persists across non-update frames to match the persistent lightMap binding.
    m_renderer->setEffectParameter("lightMapOffset",
        m_camera.worldToScreen(Vec2F(renderData.lightMinPosition) - Vec2F((float)m_lightMapBorder, (float)m_lightMapBorder)));
    m_renderer->setEffectParameter("lightmapBilinear",
        Root::singleton().configuration()->getOrDefault("lightingWorldSampleBilinear").toBool());
    m_renderer->setEffectParameter("lightmapUpscale",
        Root::singleton().configuration()->getOrDefault("lightingWorldUpscale").toFloat());
  }
  }  // render.painter.lightmap.us

  // Parallax layers (env-cache-coupled backdrop: owns the parallax retained cache + the cross-surface arbiter).
  {
    TelemetryScope s(s_painterParallax);
    m_backdropPass->renderParallax(m_camera, {renderData.skyRenderData, renderData.parallaxLayers},
        *m_environmentPainter, backdropParams, ablateParallax);
  }

  // Main world layers -- the interleaved tile / entity / particle / drawable / bar body, owned by WorldPass.
  // THE SLICE, and note the asymmetry with the backdrop's: four of these six are CONSUMED by the call --
  // hollowed, elements moved-from -- which is why WorldPass::Input holds non-const references and cannot
  // be the const view BackdropPass::Input is. Anything reading renderData's overlays, nametags or entity
  // drawables AFTER this line is reading a husk.
  {
    TelemetryScope s(s_painterBody);
    m_worldPass->renderWorld(m_camera, {renderData.entityDrawables, renderData.backgroundOverlays,
        renderData.foregroundOverlays, renderData.nametags, renderData.particles, renderData.overheadBars});
  }

  // Bracket INSIDE the gate, Cadence::Call. The dim overlay only draws when the world is dimmed; declared
  // Frame with the bracket outside, this timer recorded a ~0us sample on every undimmed frame -- 100% of its
  // records at Surface Outpost, so it read as a measured cost of zero rather than as "did not run". A pass
  // that did not happen has no cost to report, and saying 0 is a claim, not an absence.
  {
  TelemetryScope s(s_painterEpilogue);
  auto dimLevel = round(renderData.dimLevel * 255);
  if (dimLevel != 0) {
    m_renderer->gpuTimer().begin("render.pass.compose.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail, MetricUnit::Microseconds, MetricClock::GpuTimeline});
    m_renderer->render(renderFlatRect(RectF::withSize({}, Vec2F(m_camera.screenSize())), Vec4B(renderData.dimColor, dimLevel), 0.0f));
    m_renderer->gpuTimer().end("render.pass.compose.gpu_us");
  }

  // Rung 0: surface the per-pass GPU timings (populated only under deep telemetry) on the /debug HUD.
  for (auto const& key : {"render.pass.environment.gpu_us", "render.pass.parallax.gpu_us",
                          "render.pass.world.gpu_us", "render.pass.compose.gpu_us"}) {
    if (auto us = m_renderer->gpuTimer().lastMicros(key))
      LogMap::set(String(key), strf("{:05d}us", *us));
  }

  int64_t textureTimeout = m_assets->json("/rendering.config:textureTimeout").toInt();
  m_worldPass->cleanup(textureTimeout);
  m_environmentPainter->cleanup(textureTimeout);
  }  // render.painter.epilogue.us
}

void WorldPainter::adjustLighting(WorldRenderData& renderData) {
  // The last unnamed producer-side lighting cost on the render thread (#171). The chain below is
  // pure delegation -- WorldPass::adjustLighting forwards straight to TilePainter::adjustLighting,
  // where the per-tile work actually happens -- so the timer sits at the OUTERMOST call: it then
  // covers the whole chain, including anything a future layer adds in between. Owner Frame, Detail,
  // for the reason given at lighting.produce.entities.us in StarWorldClient. The handle is a member
  // registered in the constructor, so the key is never ABSENT.
  TelemetryScope s(s_adjustLightingTimer);
  m_worldPass->adjustLighting(renderData);
}

}
