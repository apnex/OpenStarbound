INCLUDE_DIRECTORIES (
    ${STAR_EXTERN_INCLUDES}
    ${STAR_CORE_INCLUDES}
    ${STAR_BASE_INCLUDES}
    ${STAR_PLATFORM_INCLUDES}
    ${STAR_GAME_INCLUDES}
    ${PROJECT_SOURCE_DIR}/rendering
  )

# Add top-level test directory so  gtest-all.cc can find its own include file
INCLUDE_DIRECTORIES (.)

SET (star_core_tests_SOURCES
      gtest/gtest-all.cc

      core_tests_main.cpp

      algorithm_test.cpp
      block_allocator_test.cpp
      blocks_along_line_test.cpp
      btree_database_test.cpp
      btree_test.cpp
      byte_array_test.cpp
      clock_test.cpp
      color_test.cpp
      container_test.cpp
      encode_test.cpp
      exception_perf_test.cpp
      file_test.cpp
      hash_test.cpp
      host_address_test.cpp
      ref_ptr_test.cpp
      json_test.cpp
      flat_hash_test.cpp
      formatted_json_test.cpp
      line_test.cpp
      lua_test.cpp
      lua_json_test.cpp
      math_test.cpp
      multi_table_test.cpp
      net_states_test.cpp
      ordered_map_test.cpp
      ordered_set_test.cpp
      periodic_test.cpp
      poly_test.cpp
      random_test.cpp
      rect_test.cpp
      retained_surface_test.cpp
      serialization_test.cpp
      spatial_hash_test.cpp
      static_vector_test.cpp
      small_vector_test.cpp
      sha_test.cpp
      shell_parse.cpp
      string_test.cpp
      strong_typedef_test.cpp
      telemetry_test.cpp
      thread_test.cpp
      worker_pool_test.cpp
      variant_test.cpp
      vlq_test.cpp
    )
ADD_EXECUTABLE (core_tests
  $<TARGET_OBJECTS:star_extern> $<TARGET_OBJECTS:star_core>
  ${star_core_tests_SOURCES})
TARGET_LINK_LIBRARIES (core_tests ${STAR_EXT_LIBS})
ADD_TEST (NAME core_tests WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/core_tests)

SET (star_game_tests_SOURCES
      gtest/gtest-all.cc

      game_tests_main.cpp

      StarTestUniverse.cpp
      assets_test.cpp
      animated_part_set_test.cpp
      drawable_cache_test.cpp
      lighting_telemetry_test.cpp
      lighting_spread_test.cpp
      lighting_point_test.cpp
      temporal_light_gate_test.cpp
      cellular_lighting_test.cpp
      function_test.cpp
      item_test.cpp
      root_test.cpp
      server_test.cpp
      entity_dormancy_test.cpp
      entity_map_test.cpp
      spawn_test.cpp
      stat_test.cpp
      status_context_test.cpp
      tile_array_test.cpp
      world_geometry_test.cpp
      universe_connection_test.cpp
    )
ADD_EXECUTABLE (game_tests
  $<TARGET_OBJECTS:star_extern> $<TARGET_OBJECTS:star_core> $<TARGET_OBJECTS:star_base> $<TARGET_OBJECTS:star_game>
  ${star_game_tests_SOURCES})
TARGET_LINK_LIBRARIES (game_tests ${STAR_EXT_LIBS})
ADD_TEST (NAME game_tests WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/game_tests)

# Layer-1 architecture gate: the sovereign render-surface module must not name OpenGlRenderer in code.
# Self-locating shell+python lint, no build artifact needed. See scripts/layering-lint.sh.
ADD_TEST (NAME layer1_layering COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/layering-lint.sh)

# LAYER-3 AIR-GAP RATCHET. Same instrument, different needle and a per-file CEILING rather than zero.
#
# A sovereign pass should be a pure function of its parameters, so Root::singleton() in a pass body is
# the reach-into-the-world coupling the decomposition exists to remove. It is NOT zero today, and the
# ceilings below are deliberately set at the CURRENT counts rather than at the target.
#
# Why a ratchet and not a prohibition: the residual has been GROWING, and the growth came from a CORRECT
# fix. StarBackdropPass's 8th read (envMaxDriftStepPx) arrived with #177, which fixed a real defect the
# Director could see -- choppy stars during ship flight. A zero gate would have scored that fix as a
# violation and invited someone to route around the gate; a ceiling lets it land while making the NEXT
# author edit a number here, which is exactly the moment to ask whether the value belongs in a params
# struct resolved at the boundary instead.
#
# LOWER THESE AS THE PAY-DOWN LANDS. The lint reports how far below its ceiling it is and tells you to
# tighten it. Raising one is allowed but must be argued in the commit message.
# RELOCATION IS NOT A ROUTE TO GREEN, AND THAT RULE COST SOMETHING TO LEARN. Until #183 this gate
# metered THREE files holding 2 of the subsystem's 17 reads. That gap was not incidental: closing the
# Air-Gap MOVES reads to the composition root -- that IS the design -- so a gate covering only the
# passes is satisfiable by relocation. It already had been. BackdropPass went 8 -> 0 in 1e46f71c,
# genuinely correct work, and not one of those eight reads left the subsystem: they moved into
# StarWorldPainter.cpp, which no gate touched. The ratchet scored a perfect result for a net change of
# zero in coupling.
#
# So every file that can RECEIVE a relocated read now carries a ceiling, and a read moved from a pass
# into the orchestrator counts against the orchestrator. That is the whole point: the next author who
# relocates one has to edit a number here and say why -- exactly the moment to ask whether the value
# belongs in a params struct resolved at the boundary instead.
ADD_TEST (NAME render_layering COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/layering-lint.sh
          --needle "Root::singleton"
          source/rendering/StarBackdropPass.cpp=0
          source/rendering/StarWorldPass.cpp=0
          source/rendering/StarGpuLightmapPass.cpp=0
          source/rendering/StarWorldPainter.cpp=9
          source/rendering/StarTilePainter.cpp=3
          source/rendering/StarTextPainter.cpp=3
          source/rendering/StarEnvironmentPainter.cpp=0
          source/rendering/StarDrawablePainter.cpp=0)
# RATCHET LOG -- lower these, never raise one without saying why in the commit message.
#   2026-07-25  BackdropPass    8 -> 0  (#137: BackdropParams resolved at the boundary by WorldPainter)
#               WorldPass       2        the remaining pass residual; same treatment, not yet done
#               LightmapPass    0        clean since extraction -- though BOUGHT by WorldPainter's reads
#   2026-07-26  WorldPass       2 -> 0  FIRST LOWERING, and the first REMOVAL rather than relocation:
#                                       both reads were `m_assets = Root::singleton().assets()`, and
#                                       WorldPainter already held a fresh handle at both call sites, so
#                                       handing it in cost the orchestrator nothing. Subsystem 17 -> 15.
#                                       NO L3 PASS NOW REACHES FOR A GLOBAL.
#   2026-07-26  #183 extends the ceilings to every file that can receive a relocated read:
#               WorldPainter    9        8 of these ARRIVED from BackdropPass. Correct architecture,
#                                        previously unmetered. This is the number to watch.
#               TilePainter     3        pre-decomposition; untouched by the campaign
#               TextPainter     3        pre-decomposition; untouched by the campaign
#               EnvironmentPainter 0     PROHIBITION, not a ratchet -- it is clean, keep it clean
#               DrawablePainter    0     PROHIBITION, not a ratchet -- it is clean, keep it clean
#
# EVERY REMAINING READ IS METERED. Cross-checked before landing: scripts/render-inventory.py and
# layering-lint.sh use DIFFERENT comment-stripping strategies (a python regex pass vs the lint's own),
# so agreement was CONFIRMED rather than assumed -- both report 15 across the five files added here,
# plus the 2 already metered. If you add a render file, add it here too, or the total silently drifts
# from the generated table in docs/render/architecture-3-target-state.md.

# DOC-FRESHNESS GATE (#179). The architecture doc's Air-Gap counts are GENERATED into a marker block by
# `scripts/render-inventory.py --inject`; this asserts the COMMITTED block still matches the tree.
#
# Measuring beside the doc was not enough. render-inventory.py already existed and printed correct
# numbers while the doc went on asserting that BackdropPass held 7 Root::singleton() reads against a
# tree measuring 0 -- because a measurement a human pastes is hand-typed again the instant it lands. A
# cold agent reading that doc is sent to pay down work that does not exist.
#
# It gates the COUPLING residual only, never line counts. Line counts churn on every commit, so gating
# them would redden CI constantly -- the zero-tolerance failure mode the render_layering ratchet above
# exists to avoid. A wrong line count is cosmetic; a wrong singleton count is directional.
ADD_TEST (NAME render_docs_fresh COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/render-inventory.py
          --check docs/render/architecture-3-target-state.md)

# Render-surface unit tests: the RB-1 / RB-6 CPU-side invariants of the sovereign module, exercised in isolation
# (no GL context). The two module TUs are compiled directly here -- they are sovereign (zero refs to
# OpenGlRenderer, enforced by the layer1_layering gate) and Texture is a header-abstract interface, so they need
# only core/base/extern + GL, not the whole star_application. gtest-all.cc + core_tests_main.cpp supply the
# framework and main(); these are the first tests that construct a Layer-1 component standalone.
SET (star_render_surface_tests_SOURCES
      gtest/gtest-all.cc
      core_tests_main.cpp
      render_surface_test.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../application/StarGlRenderSurface.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../application/StarGlTexturePrimitives.cpp)
ADD_EXECUTABLE (render_surface_tests
  $<TARGET_OBJECTS:star_extern> $<TARGET_OBJECTS:star_core> $<TARGET_OBJECTS:star_base>
  ${star_render_surface_tests_SOURCES})
TARGET_INCLUDE_DIRECTORIES (render_surface_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../application)
TARGET_LINK_LIBRARIES (render_surface_tests ${STAR_EXT_LIBS} ${STAR_EXT_GUI_LIBS})
ADD_TEST (NAME render_surface_tests WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/render_surface_tests)

# THE `NoAssets` LABEL IS NOT DECORATION -- IT IS WHETHER THE TEST RUNS IN CI AT ALL.
#
# Every testPreset in source/CMakePresets.json inherits `base`, which filters to `LABELS NoAssets`, and
# .github/workflows/build.yml drives ctest through those presets. So a registered test WITHOUT this
# label is silently skipped by every CI run -- it exists, it passes locally under a bare `ctest`, and it
# guards nothing.
#
# That is exactly what had happened to the two Layer-1 architecture gates. `layer1_layering` calls
# itself a "standing lint" that "fails the build if a code reference reappears"
# (scripts/layering-lint.sh, docs/render/layer1-architecture.md) and `render_surface_tests` is the
# only standalone unit coverage the sovereign L1 module has -- and NEITHER had ever executed in CI. The
# accidental consequence was that retained_surface_test, which happens to live inside core_tests, was the
# ONLY render-architecture check CI actually ran.
#
# Both qualify honestly: layer1_layering is a self-locating shell+python grep over source files, and
# render_surface_tests constructs Layer-1 components with NO GL context and NO assets (it links the GUI
# libs but never calls into them). Verified passing before being added here.
#
# ADD THE LABEL WHENEVER YOU ADD AN ASSET-FREE TEST. A gate nobody runs is worse than no gate, because
# the docs go on citing it.
SET_TESTS_PROPERTIES(core_tests layer1_layering render_surface_tests render_layering render_docs_fresh
                     PROPERTIES LABELS "NoAssets")
