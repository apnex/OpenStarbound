# Registered in place of the render architecture gates when CMake could not find a python3 interpreter
# (source/test/CMakeLists.txt). Its whole job is to fail loudly and say why.
#
# A gate that silently does not exist reads exactly like a gate that passes -- that is the defect this
# whole gate set was built to close, so the gate set must not commit it itself.
message(FATAL_ERROR
  "The render architecture gates could not be registered: no python3 interpreter was found at configure "
  "time.\n"
  "  layer1_layering    -- Layer-1 sovereignty (the render-surface module must not name OpenGlRenderer)\n"
  "  render_layering    -- the Layer-3 Air-Gap ratchet (per-file Root::singleton ceilings)\n"
  "  render_docs_fresh  -- the generated coupling counts in docs/render/ still match the tree\n"
  "Install python3 and re-configure. Until then these three architectural invariants are UNCHECKED; "
  "this test exists so that fact is visible in ctest output instead of being invisible.")
