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

# NAME THE INTERPRETER. NEVER RELY ON A SHEBANG FOR ANYTHING CTEST RUNS (#192).
#
# The three script gates below were registered as `COMMAND <path>/foo.sh`. That works on Linux and macOS
# and is DEAD ON ARRIVAL on Windows: ctest starts a test with CreateProcess, which honours neither `#!`
# nor PATHEXT/file associations, so a .sh -- and equally a .py -- named directly as COMMAND cannot start.
# On windows-latest all three reported `***Not Run`, every run, for weeks. They were registered, they
# carried the NoAssets label, the preset filtered them IN, and they guarded nothing; ctest scores Not Run
# as FAILED, so they also reddened that job unconditionally. The label was doing its job. The COMMAND
# could not start. This is the same claim-vs-enforcement gap the gates themselves exist to close, one
# level up -- and it was found by mechanising the label rule (#187), not by anyone reading CI.
#
# layering-lint was ported bash -> python for this (scripts/layering-lint.py, output-identical on both
# the pass and fail paths, verified before landing). Python3 is the one interpreter present on all six
# CI images, and naming ${Python3_EXECUTABLE} explicitly is what makes the gate portable.
find_package(Python3 COMPONENTS Interpreter)

if(NOT Python3_Interpreter_FOUND)
  # LOUD, NOT SILENT. Dropping the registrations here would recreate the exact defect above: a gate that
  # does not exist reads identically to a gate that passes. One red test naming the cause is the honest
  # state, and it does not block configuring or building.
  message(WARNING
    "python3 was not found: the render architecture gates (layer1_layering, render_layering, "
    "render_docs_fresh) cannot run. A placeholder test is registered in their place so this is visible "
    "in ctest output rather than silently absent.")
  ADD_TEST (NAME render_gates_unavailable
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/gates-need-python.cmake)
else()

# Layer-1 architecture gate: the sovereign render-surface module must not name OpenGlRenderer in code.
# Self-locating python lint, no build artifact needed. See scripts/layering-lint.py.
ADD_TEST (NAME layer1_layering COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/layering-lint.py)

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
ADD_TEST (NAME render_layering COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/layering-lint.py
          --needle "Root::singleton"
          source/rendering/StarBackdropPass.cpp=0
          source/rendering/StarWorldPass.cpp=0
          source/rendering/StarGpuLightmapPass.cpp=0
          source/rendering/StarWorldPainter.cpp=9
          source/rendering/StarTilePainter.cpp=3
          source/rendering/StarTextPainter.cpp=3
          source/rendering/StarEnvironmentPainter.cpp=0
          source/rendering/StarDrawablePainter.cpp=0
          source/rendering/StarAssetTextureGroup.cpp=2
          source/rendering/StarFontTextureGroup.cpp=0
          source/rendering/StarAnchorTypes.cpp=0)
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
#   2026-07-26  #137 claims the three previously UNLAYERED files, so the count stops lying by omission:
#               AssetTextureGroup  2      a reload-listener registration + an assets handle. These were
#                                         always there; no layer was looking. Measured residual 15 -> 17.
#                                         NOTE it is also consumed by source/frontend and source/windowing
#                                         -- shared service, not ours alone. Look outside source/rendering
#                                         before treating this as render-subsystem debt.
#               FontTextureGroup   0      PROHIBITION -- glyph cache, clean
#               AnchorTypes        0      PROHIBITION -- type vocabulary, clean
#
# EVERY REMAINING READ IS METERED. Cross-checked before landing: scripts/render-inventory.py and
# layering-lint.py use DIFFERENT comment-stripping strategies (a python regex pass vs the lint's own),
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
ADD_TEST (NAME render_docs_fresh COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/render-inventory.py
          --check docs/render/architecture-3-target-state.md)

# CONFIG DECLARATION GATE (#185). Every key read through Configuration::getOrDefault must have a declared
# default in one of the default blocks.
#
# getOrDefault exists so that a knob has ONE declaration -- the default block -- instead of one per call
# site. Its callers therefore consume the value with a bare .toBool()/.toFloat()/.toUInt() and no second
# literal, which is the whole point and which is also why an undeclared key must fail the BUILD rather
# than throw in the render path at runtime.
#
# The rule this replaces was not written down anywhere, and five render keys had a shipped default that
# disagreed with the literal typed at the call site (lightingGpu true/false, lightingGpuSpreadIterations
# 32/64, lightingTonemap true/false, lightingWorldUpscale 2.0/1.0, envRefreshInterval 4 against a
# `/rendercache status` readout of 1). `newLighting` was declared in none of the three blocks at all.
ADD_TEST (NAME config_declared COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/config-lint.py)

# THE GAME<->RENDER BOUNDARY, measured and ratcheted (docs/render/game-render-boundary.md).
#
# A headless SERVER already ships -- starbound_server links no render library, and zero files in source/game
# name Renderer/OpenGl/GL_/glew. What does not exist is a headless CLIENT: WorldClient unconditionally
# produces WorldRenderData, every Entity carries render()/renderLightSources() vtable slots a server never
# calls, and view code like TileDrawer lives in the sim library.
#
# The ceiling is the PUSH-SINK + FRAME-MODEL vocabulary only -- RenderCallback, WorldRenderData,
# EntityDrawables, EntityRenderLayer, EntityHighlightEffect, renderLightSources. `Drawable` is deliberately
# EXCLUDED: an entity describing its own appearance is a legitimate game-layer duty, and folding it in would
# produce one number (708) that nobody can act on instead of one (213) that names the actual blocker.
#
# A TOTAL, not per-file, and that is the opposite call from the render_layering ratchet above -- deliberately.
# #183's lesson was that a per-file ratchet over a SUBSET is satisfiable by relocation. Here it is not:
# moving a reference between two star_game files leaves the total unchanged, and the only ways down are to
# delete it or move the code OUT of star_game, which is the goal. Gameable proxy and real target coincide.
#
# SET AT TODAY'S COUNT, like every ratchet this campaign has shipped. Lower it as the pay-down lands; raising
# it is allowed and must be argued in the commit message.
ADD_TEST (NAME boundary_ratchet COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/boundary-inventory.py --max-sink 213)

# Same shape as render_docs_fresh: the doc's numbers are GENERATED into a marker block, and this asserts the
# committed block still matches the tree. A measurement a human pastes is hand-typed again the instant it
# lands (#179).
ADD_TEST (NAME boundary_fresh COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/boundary-inventory.py
          --check docs/render/game-render-boundary.md)

# The whole-system boundary document: nine generated Mermaid diagrams, every number in them measured from
# the tree. Same rule as render_docs_fresh and boundary_fresh, applied one altitude up -- a hand-drawn
# architecture diagram in this repository has never survived a week, and a diagram is worse than a table
# because its wrongness is not even legible as a number.
#
# This gate ALSO fails on two conditions the generator refuses to draw through: duplicate header basenames
# (include attribution resolves by basename, so a collision silently mis-attributes edges) and a tier table
# that contradicts the grants.
ADD_TEST (NAME arch_graph_fresh COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/arch-graph.py
          --check docs/architecture/system-boundaries.md)

# GRANT SWEEP. The headless-client design's grant table is the one artifact in it with no instrument
# behind it, and two defects sat there: `host` granted to nobody while 8 files needed it, and both
# host backends missing `platform`. Neither would have compiled. This maps today's files onto their
# target-state owners, resolves includes TRANSITIVELY (the preprocessor does), and fails on any
# grant the tree needs but the table omits -- plus a ratchet on the 43 crossings the design exists
# to delete, which may only go down.
ADD_TEST (NAME grant_sweep COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/grant-sweep.py --check)

# LOOP INVENTORY. The design's loop count was wrong twice, both times found by inspection rather than
# by a check -- once missing the fixed-timestep accumulator nested in the frame loop, once missing
# UniverseServer::run entirely. This finds every loop that YIELDS TO TIME (sleeps, waits, consults a
# rate governor, or is a thread body), requires each to be declared, and reconciles the declared set
# against the *Loop elements the spec models. A third miss was the instrument's own: it globbed
# Star*.cpp and only matched qualified/inline member definitions, so server/main.cpp's free `main()`
# and its loop were invisible -- and a matching false "target-state-only" declaration kept it green.
ADD_TEST (NAME loop_inventory COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/loop-inventory.py --check)

# SPEC CONSISTENCY. The headless-client design is described twice on purpose -- Section 4 is the
# compile projection, Section 5 the runtime one -- and two descriptions of one model drift. Eight
# defects were found by running these comparisons by hand before this became a gate, including a
# diagram edge contradicting its own grant row and a stale component tally. The one that earned the
# gate is the GRAFT RULE: every runtime edge must be legal in the compile projection, and its first
# run rejected two, because no host shared a contract with `rendering` and so nothing could trigger
# the paint. Ten injected defects were each confirmed to fire, including a missing projection marker.
ADD_TEST (NAME spec_consistency COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/spec-consistency.py --check)

# DEDUPLICATION MEASURE. "Why this is fully deduplicated" was the design's one north-star claim with
# no instrument behind it. Section 5's framing supplies one: a TICK is the root of a call tree, so
# closure(clientTick) INTERSECT closure(presentTick) is exactly the code both machines must ship once
# the two sides split. Measured from objdump relocations -- USE, not the include PERMISSION that
# grant-sweep measures. Ratchet at 596 symbols outside the foundations and contracts; may only fall.
# Not in the Gates workflow: that job runs scripts without building, where this would permanently
# SKIP and a permanent skip reads like a pass.
ADD_TEST (NAME dedup_measure COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/dedup-measure.py --check)

# COMPOSITION GRAPHS. The whole-system map answers "what exists"; it cannot answer "what does THIS
# binary link". Derived from the grant table rather than drawn, because three hand-made diagrams would
# be three more artifacts to drift. Their first generation raised two questions the map hides: `server`
# links `scene`, and `client_headless` links `windowing` and `frontend`.
ADD_TEST (NAME composition_graphs COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/composition-graphs.py --check)

# LINK SWEEP. Every gate above reads the SOURCE. None can see what the linker did, and that is not a
# small blind spot: a source-altitude check reports PASS over a 73 MB dedicated server containing a
# software audio mixer, because `server` IS granted `game` and `Mixer` IS in `base`. The declaration
# is satisfied and the binary is still wrong. This attributes every symbol in every built ENTRYPOINT
# back to a component and asserts it lies inside that composition's grant closure.
#
# Ratcheted, because the tree has two real leaks today (server links `mixing` and `scene`). Ceilings
# live in the script beside the reason each exists.
#
# LINUX ONLY, AND DELIBERATELY SO. It parses ELF `nm -A` output and maps object paths through the
# OBJECT-library layout; there is no `nm` under MSVC, and the binaries carry a .exe suffix. Registering
# it on all six platforms would put Windows and macOS red the day it landed -- which is #201 (CI-5)
# exactly, a gate that was red on Windows from birth for a path-separator assumption. Absent on those
# platforms is honest; red on those platforms is noise that trains people to ignore the matrix.
#
# It IS labelled NoAssets, unlike its sibling dedup_measure's placement suggests: it needs no assets/,
# only a completed build, and build.yml runs ctest after building. It refuses to report OK when it
# finds no binary to measure, so a build that produced nothing fails loudly rather than passing empty.
IF (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  ADD_TEST (NAME link_sweep COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/link-sweep.py --check)
ENDIF ()

# HOST API NEUTRALITY -- and it exists because grant_sweep is STRUCTURALLY BLIND to it.
#
# The design says `host_sdl` owns the window as an OS object and knows nothing about how that window
# is drawn to. grant_sweep agrees: it measures coupling between OUR components via includes, and it
# counts host_sdl -> gpu at 1 and host_sdl -> gpu_opengl at 1, both already on the removal ratchet.
#
# It is wrong by an order of magnitude, and not through any defect of its own. The real coupling is
# expressed through SDL'S GL SUB-API -- a third party -- which no include sweep can see:
#
#   SDL_WINDOW_OPENGL in the window flags, 8x SDL_GL_SetAttribute pinning a 3.2 core context,
#   SDL_GL_CreateContext / DestroyContext, SDL_GL_SetSwapInterval, SDL_GL_SwapWindow
#
# 15 references. Delete `make_shared<OpenGlRenderer>()` and grant_sweep reads ZERO while the host
# still creates a GL context and swaps GL buffers -- at which point `client_sdl_gpu`, whose entire
# claim is "identical except for the backend", would not work.
#
# ANY coupling routed through a third-party API is invisible to the include sweep. This is the first
# instance found; it is unlikely to be the last, and Section 6 now says so.
# DRIVE TABLE. The element register says what each element IS; it never said what STARTS it. That
# relationship existed only as edges inside a mermaid diagram, so answering "what drives this?" meant
# tracing arrows by eye -- and when the Director asked for it as a table it had to be assembled by
# hand, which is the definition of a missing artifact. Derived from the runtime projection so it
# cannot drift, and it FAILS on an element with neither an incoming edge nor a declared reason:
# undriven is a design statement or a hole, never neither.
IF (Python3_Interpreter_FOUND)
  ADD_TEST (NAME drive_table COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/drive-table.py --check)
ENDIF ()

# TREE MAP. Zones became directories, so the register is now a filesystem layout. A hand-written tree
# beside a 41-row table would be a second declaration of one fact -- the defect class that produced
# spec-model.py. Derived and checked for freshness like every other generated block.
IF (Python3_Interpreter_FOUND)
  ADD_TEST (NAME tree_map COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/tree-map.py --check)
ENDIF ()

# PROSE CLAIMS. Sixteen gates check tables, diagrams and grants. NONE of them read the words, and the
# words are where claims rot -- no generator owns prose and no parser read it. Every gate was green
# while the document said "three clocks" (five), called `participant` `client` (renamed hours before),
# described `net` as "CONTRACT, SEAM" (a zone that no longer exists), and referenced D8/D9, which had
# no rows in the decisions table. Its first run found 16 such claims.
#
# It checks the document against ITSELF, not against the tree: numbers measured from the codebase
# belong to the ratchets that own them. That limit is stated in the script rather than discovered.
# spec_measures is the only gate here that greps C++ on the design's behalf. It owns the handful of
# tree measurements a DESIGN DECISION rests on, and regenerates them into Section 6. Seven of the 27
# confirmed review findings were one defect in seven costumes: "the thirteen render() bodies" (17),
# "exactly four" Lua groups (11), "43 crossings across 10 edges" (55 across 12). Each was true when
# written and each had become an argument resting on a memory.
IF (Python3_Interpreter_FOUND)
  ADD_TEST (NAME spec_measures COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/spec-measures.py --check)
ENDIF ()

IF (Python3_Interpreter_FOUND)
  ADD_TEST (NAME prose_claims COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/prose-claims.py --check)
  # ...AND THE GATE ITSELF IS GATED. prose_claims reported OK for its entire life while two of its
  # five verdicts were half-blind: STALE_ZONE could not see "has no SEAM zone at all" (nothing beside
  # it was a KIND word) and COUNT_DRIFT could not see "forty components" (it only read digits). Both
  # were found by a human reading the document, not by the gate. A green --check proves the document
  # is clean and proves nothing about the instrument. --selftest injects one known defect per verdict,
  # asserts each fires, and asserts a control sentence does NOT.
  ADD_TEST (NAME prose_claims_fires COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/prose-claims.py --selftest)
ENDIF ()

# The needle was "SDL_GL_" with a ceiling of 15, and it was blind to its own design. The seam-2 table
# in the headless-client spec names "the context" and "the window creation flags" as the GL couplings
# a neutral host must not have -- spelled `SDL_GLContext` and `SDL_WINDOW_OPENGL`, neither of which
# contains `SDL_GL_`. A ratchet invented to see coupling the include graph misses was itself missing
# three sites, on a subject its own paragraph enumerated. Widened to a regex; the true count is 18.
IF (Python3_Interpreter_FOUND)
  ADD_TEST (NAME host_api_neutral COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/layering-lint.py
            --needle "re:SDL_GL|SDL_WINDOW_OPENGL"
            source/application/StarMainApplication_sdl.cpp=18)
ENDIF ()

endif() # Python3_Interpreter_FOUND

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
TARGET_LINK_LIBRARIES (render_surface_tests ${STAR_EXT_LIBS} ${STAR_EXT_GUI_LIBS_CORE})
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
# (scripts/layering-lint.py, docs/render/layer1-architecture.md) and `render_surface_tests` is the
# only standalone unit coverage the sovereign L1 module has -- and NEITHER had ever executed in CI. The
# accidental consequence was that retained_surface_test, which happens to live inside core_tests, was the
# ONLY render-architecture check CI actually ran.
#
# Both qualify honestly: layer1_layering is a self-locating python grep over source files, and
# render_surface_tests constructs Layer-1 components with NO GL context and NO assets. Verified passing
# before being added here.
SET (star_noassets_tests core_tests render_surface_tests)
IF (Python3_Interpreter_FOUND)
  LIST (APPEND star_noassets_tests layer1_layering render_layering render_docs_fresh config_declared
                                   boundary_ratchet boundary_fresh arch_graph_fresh
                                   grant_sweep loop_inventory spec_consistency
                                   dedup_measure composition_graphs)
  IF (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    LIST (APPEND star_noassets_tests link_sweep)   # registered above only on Linux -- see there
  ENDIF ()
  LIST (APPEND star_noassets_tests host_api_neutral drive_table tree_map prose_claims
                                   prose_claims_fires spec_measures)
ELSE ()
  LIST (APPEND star_noassets_tests render_gates_unavailable)
ENDIF ()
SET_TESTS_PROPERTIES(${star_noassets_tests} PROPERTIES LABELS "NoAssets")

# ...AND THE PARAGRAPH ABOVE USED TO END WITH "ADD THE LABEL WHENEVER YOU ADD AN ASSET-FREE TEST",
# WHICH IS A REQUEST, NOT A RULE (#187). A rule nothing checks is indistinguishable from a rule nobody
# follows -- which is precisely the finding that produced the paragraph in the first place. So the two
# halves of the mechanism are now asserted here, at configure time, where they cannot be skipped:
#
#   (1) every registered test carries the label, unless it is on a named allowlist of tests that
#       genuinely need assets and are therefore knowingly absent from CI;
#   (2) the `base` testPreset still filters on exactly that label -- because if someone renames or
#       retypes the filter, every labelled test silently stops running and CI goes GREEN while
#       executing nothing. Removing the filter is loud (game_tests would run and fail for want of
#       assets); MIS-typing it is silent, and silence is the failure mode worth gating.
#
# Configure-time rather than another ctest: this catches the mistake before a build, needs no
# interpreter, and cannot itself be Not-Run. It sees the tests registered in THIS directory, which is
# every test the project has; a test added elsewhere would need this check extended to it.
SET (star_tests_needing_assets game_tests)  # game_tests boots a universe from assets/ -- cannot run in CI
GET_PROPERTY (star_registered_tests DIRECTORY PROPERTY TESTS)
SET (star_tests_missing_label "")
FOREACH (star_t IN LISTS star_registered_tests)
  IF (NOT star_t IN_LIST star_tests_needing_assets)
    GET_TEST_PROPERTY (${star_t} LABELS star_t_labels)
    IF (NOT "NoAssets" IN_LIST star_t_labels)
      LIST (APPEND star_tests_missing_label ${star_t})
    ENDIF ()
  ENDIF ()
ENDFOREACH ()
IF (star_tests_missing_label)
  MESSAGE (FATAL_ERROR
    "These tests are registered without the NoAssets label: ${star_tests_missing_label}\n"
    "Every testPreset in source/CMakePresets.json inherits `base`, which filters to LABELS NoAssets, and "
    ".github/workflows/build.yml drives ctest through those presets -- so an unlabelled test is skipped "
    "by ALL SIX CI jobs. It exists, it passes under a bare local `ctest`, and it guards nothing.\n"
    "Either add it to star_noassets_tests above, or -- if it genuinely needs assets/ and therefore "
    "cannot run in CI -- add it to star_tests_needing_assets and accept that CI will never execute it.")
ENDIF ()

FILE (READ ${CMAKE_CURRENT_SOURCE_DIR}/../CMakePresets.json star_presets_json)
STRING (JSON star_preset_count ERROR_VARIABLE star_presets_err LENGTH "${star_presets_json}" testPresets)
IF (star_presets_err)
  MESSAGE (FATAL_ERROR "Cannot read testPresets from source/CMakePresets.json: ${star_presets_err}")
ENDIF ()
SET (star_base_filter_label "")
MATH (EXPR star_preset_last "${star_preset_count} - 1")
FOREACH (star_i RANGE ${star_preset_last})
  STRING (JSON star_preset_name GET "${star_presets_json}" testPresets ${star_i} name)
  IF (star_preset_name STREQUAL "base")
    STRING (JSON star_base_filter_label ERROR_VARIABLE star_filter_err
            GET "${star_presets_json}" testPresets ${star_i} filter include label)
  ENDIF ()
ENDFOREACH ()
IF (NOT star_base_filter_label STREQUAL "NoAssets")
  MESSAGE (FATAL_ERROR
    "The `base` testPreset in source/CMakePresets.json no longer filters on the NoAssets label "
    "(found: '${star_base_filter_label}'). Every other testPreset inherits it, so this one string decides "
    "what all six CI jobs execute. If it does not match the label applied above, CI runs NOTHING and "
    "reports success. Fix the preset, or change both sides together.")
ENDIF ()
