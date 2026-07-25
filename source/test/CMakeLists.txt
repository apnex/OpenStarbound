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
ADD_TEST (NAME render_layering COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/layering-lint.sh
          --needle "Root::singleton"
          source/rendering/StarBackdropPass.cpp=0
          source/rendering/StarWorldPass.cpp=2
          source/rendering/StarGpuLightmapPass.cpp=0)
# RATCHET LOG -- lower these, never raise one without saying why in the commit message.
#   BackdropPass  8 -> 0  (#137: BackdropParams resolved at the boundary by WorldPainter)
#   WorldPass     2        the remaining residual; same treatment, not yet done
#   LightmapPass  0        clean since extraction -- though BOUGHT by WorldPainter doing its reads

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
SET_TESTS_PROPERTIES(core_tests layer1_layering render_surface_tests render_layering PROPERTIES
                     LABELS "NoAssets")
