#!/usr/bin/env python3
# build-manifest.py -- attribute the net fork diff (2ea33530 -> dev/upstream-merge) to reorg clusters.
# Ordered rules; FIRST match wins. SHARED = touched by >1 cluster -> needs per-hunk attribution (shared-hunks.tsv);
# everything else is a whole-file-owned path. Emits manifest.tsv (owned) + shared.txt + coverage report.
import re, subprocess, sys, os

ROOT = subprocess.check_output(["git","rev-parse","--show-toplevel"]).decode().strip()
os.chdir(ROOT)
files = subprocess.check_output(
    ["git","diff","--name-only","2ea33530","dev/upstream-merge"]).decode().splitlines()

# (regex, cluster). SHARED first (multi-cluster / god-object files needing hunk attribution).
RULES = [
  # ---- SHARED: render backend + world painter + config/command/client + base lighting ----
  (r"source/application/StarRenderer_opengl\.(cpp|hpp)$", "SHARED"),
  (r"source/application/StarRenderer\.hpp$", "SHARED"),
  (r"source/rendering/StarWorldPainter\.(cpp|hpp)$", "SHARED"),
  (r"source/game/StarRootLoader\.cpp$", "SHARED"),
  (r"source/frontend/StarClientCommandProcessor\.(cpp|hpp)$", "SHARED"),
  (r"source/client/StarClientApplication\.(cpp|hpp)$", "SHARED"),
  (r"assets/opensb/rendering/opengl\.config$", "SHARED"),
  (r"assets/opensb/rendering/effects/(world|lightingPassthrough)\.", "SHARED"),
  (r"source/base/StarCellularLight(ing|Array)\.(cpp|hpp)$", "SHARED"),
  # ---- SHARED: cross-perf-group god-objects ----
  (r"source/game/StarObject\.(cpp|hpp)$", "SHARED"),          # server-tick + animation-drawable + entity-dispatch
  (r"source/game/interfaces/StarEntity\.(cpp|hpp)$", "SHARED"),# entity-dispatch + server-tick(dormancy seam)
  (r"source/game/StarPlant\.(cpp|hpp)$", "SHARED"),           # server-tick(wind) + entity-dispatch(accessor)
  (r"CMakeLists\.txt$", "SHARED"),                            # build wiring: many clusters add targets/sources

  # ---- render/layer1 (owned) ----
  (r"source/application/StarGl(RenderSurface|TexturePrimitives)\.", "render/layer1"),
  (r"source/application/StarRenderDiagnostics\.hpp$", "render/layer1"),
  (r"source/test/render_surface_test\.cpp$", "render/layer1"),
  (r"scripts/layer1-layering-lint\.sh$", "render/layer1"),
  # ---- render/layer3 (owned; CDL folds in) ----
  (r"source/rendering/StarGpuLightmapPass\.", "render/layer3"),
  (r"assets/opensb/rendering/effects/lighting(Point|Spread|Upscale)\.", "render/layer3"),
  (r"source/game/StarWorldRenderData\.hpp$", "render/layer3"),
  (r"source/test/(lighting_point|lighting_spread|cellular_lighting)_test\.cpp$", "render/layer3"),
  # ---- perf/server-tick ----
  (r"source/game/(StarWorldServer(Thread)?|StarDamageManager|StarMovementController|StarCollisionBlock)\.", "perf/server-tick"),
  (r"source/core/StarPeriodic\.hpp$", "perf/server-tick"),
  (r"source/base/StarCellularLiquid\.hpp$", "perf/server-tick"),
  (r"source/test/(entity_dormancy|periodic)_test\.cpp$", "perf/server-tick"),
  # ---- perf/entity-dispatch ----
  (r"source/game/interfaces/", "perf/entity-dispatch"),
  (r"source/game/StarEntityMap\.", "perf/entity-dispatch"),
  (r"source/core/StarSpatialHash2D\.hpp$", "perf/entity-dispatch"),
  (r"source/game/(StarMonster|StarNpc|StarPlayer|StarProjectile|StarVehicle|StarStagehand|StarSky|StarItemDrop|StarPlantDrop|StarTechController|StarToolUser|StarWireProcessor|StarContainerObject)\.", "perf/entity-dispatch"),
  (r"source/test/(spatial_hash|entity_map)_test\.cpp$", "perf/entity-dispatch"),
  # ---- perf/animation-drawable ----
  (r"source/game/StarNetworkedAnimator\.", "perf/animation-drawable"),
  (r"source/base/StarAnimatedPartSet\.", "perf/animation-drawable"),
  (r"source/test/(drawable_cache|animated_part_set)_test\.cpp$", "perf/animation-drawable"),
  # ---- perf/world-client-lighting ----
  (r"source/game/StarWorldClient\.", "perf/world-client-lighting"),
  (r"source/game/StarTemporalLightingGate\.hpp$", "perf/world-client-lighting"),
  (r"source/test/temporal_light_gate_test\.cpp$", "perf/world-client-lighting"),
  # ---- perf/core ----
  (r"source/core/(StarJson|StarLua|StarNetElement)", "perf/core"),
  (r"source/game/scripting/(StarLuaRoot|StarLuaComponents)\.", "perf/core"),
  (r"source/game/StarStatusController\.", "perf/core"),
  (r"source/core/StarException_unix\.cpp$", "perf/core"),
  (r"source/test/(json|lua|net_states|exception_perf|status_context|lua_json)_test\.cpp$", "perf/core"),
  # ---- tooling ----
  (r"source/core/StarTelemetry(Reporter)?\.", "tooling/telemetry"),
  (r"source/test/(telemetry|lighting_telemetry)_test\.cpp$", "tooling/telemetry"),
  (r"scripts/(render-gate|deploy-install)\.sh$", "tooling/render-harness-oracle"),
  (r"source/test/(StarTestUniverse|server)_test?\.(cpp|hpp)$", "tooling/render-harness-oracle"),
  (r"source/test/StarTestUniverse\.", "tooling/render-harness-oracle"),
  (r"(vcpkg-overlay-ports/|triplets/|\.gitignore$)", "tooling/build-toolchain"),
  # ---- stragglers (2nd pass) ----
  (r"assets/opensb/(worldserver|universe_server)\.config\.patch$", "perf/server-tick"),  # server-perf gates
  (r"assets/opensb/client\.config\.patch$", "SHARED"),                                    # render L2 + others
  (r"source/frontend/StarMainInterface\.(cpp|hpp)$", "tooling/telemetry"),                # HUD telemetry face
  (r"source/windowing/", "tooling/telemetry"),                                            # HUD widget plumbing
  (r"source/game/objects/StarContainerObject\.", "SHARED"),                               # dormancy wake + drawable
  (r"source/game/objects/StarFarmableObject\.", "perf/entity-dispatch"),                  # entity accessor override
  (r"source/game/StarWorldImpl\.hpp$", "SHARED"),                                          # collision + entity query
  (r"source/game/StarWorld(Generation|Tiles)\.", "SHARED"),                               # uncertain -> hunk review
  (r"source/game/scripting/StarEntityLuaBindings\.", "perf/entity-dispatch"),
  (r"source/game/scripting/(StarMovementControllerLuaBindings|StarLuaActorMovementComponent)\.", "perf/server-tick"),
  (r"source/game/scripting/StarStatusControllerLuaBindings\.", "perf/core"),
  (r"source/game/scripting/StarWorldLuaBindings\.", "perf/core"),                          # json fast-path caller
  (r"source/rendering/StarEnvironmentPainter\.", "render/layer3"),                         # gpu-ladder R-D overdraw
  # ---- docs ----
  (r"^docs/", "docs"),
]

owned, shared, unmapped = [], [], []
for f in files:
    hit = next((c for pat,c in RULES if re.search(pat,f)), None)
    if hit is None: unmapped.append(f)
    elif hit == "SHARED": shared.append(f)
    else: owned.append((f,hit))

with open("scripts/reorg/manifest.tsv","w") as m:
    for f,c in sorted(owned): m.write(f+"\t"+c+"\n")
with open("scripts/reorg/shared.txt","w") as s:
    for f in sorted(shared): s.write(f+"\n")

print("COVERAGE: %d files total | %d owned | %d SHARED (need hunk attribution) | %d UNMAPPED" %
      (len(files), len(owned), len(shared), len(unmapped)))
from collections import Counter
print("\nowned per cluster:")
for c,n in sorted(Counter(c for _,c in owned).items()): print("  %-28s %d" % (c,n))
if unmapped:
    print("\nUNMAPPED (need a rule):")
    for f in unmapped: print("  "+f)
