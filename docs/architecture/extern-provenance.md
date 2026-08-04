# Vendored provenance: source/extern

Generated from `docs/architecture/extern-provenance.json` by `scripts/extern-provenance.py`.
Do not edit the block below; edit the register and re-inject. `run-gates.sh` fails on drift.

<!-- BEGIN GENERATED: scripts/extern-provenance.py -->
<!-- Regenerate with: python3 scripts/extern-provenance.py --inject -->

| artefact | version | reach | licence | upstream | attributed |
|---|---|---|---|---|---|
| `curve25519` | unversioned-vendor-drop | direct | MIT | https://github.com/msotoodeh/curve25519 | **none** |
| `fast_float` | unversioned-vendor-drop | header | Apache-2.0 OR MIT OR BSL-1.0 | https://github.com/fastfloat/fast_float | **none** |
| `fmt` | 10.2.1 | direct | MIT | https://github.com/fmtlib/fmt | **none** |
| `imgui_lua_bindings` | unversioned-vendor-drop | direct | UNSTATED-IN-VENDORED-COPY | https://github.com/patrickriordan/imgui_lua_bindings | **none** |
| `lua` | 5.3.6 | direct | MIT | https://www.lua.org/ | doc/OPENSOURCE.md |
| `rpmalloc` | unversioned-vendor-drop | conditional (STAR_USE_RPMALLOC) | Public Domain | https://github.com/mjansson/rpmalloc | **none** |
| `xxhash` | 0.8.1 | direct | BSD-2-Clause | https://github.com/Cyan4973/xxHash | doc/OPENSOURCE.md |
| `xxhash-x86dispatch` | 0.8.1 | unreachable | BSD-2-Clause | https://github.com/Cyan4973/xxHash | doc/OPENSOURCE.md |

### Why each is vendored

- **curve25519** — Ed25519 signing and X25519 key exchange for the network handshake. No release tags in the vendored drop, so no version is derivable and none is claimed.
- **fast_float** — Locale-independent string-to-float used by the Json parser. Header-only single file; the vendored copy carries no version macro.
- **fmt** — Formatting for strf/Logger across the whole tree. Vendored rather than packaged so the format-string ABI cannot shift under a vcpkg baseline bump mid-campaign.
- **imgui_lua_bindings** — Exposes the debug ImGui surface to Lua. The vendored copy carries NO licence header, which is a genuine attribution gap rather than a formatting one -- recorded here so it is visible instead of absent.
- **lua** — The scripting language the entire mod surface is written against. Vendored by upstream Starbound; the interpreter is not swappable for a packaged one without changing script semantics.
- **rpmalloc** — Optional allocator, one of three (jemalloc / mimalloc / rpmalloc). malloc.c is the operator-new shim and is NOT in any CMake list -- rpmalloc.c #includes it, so it compiles only through that.
- **xxhash** — Content hashing for the render caches and the harness frame hash. xxh3.h is reached from source/core/StarXXHash.hpp via the include path, NOT through CMakeLists -- it is live despite being named in no build list.
- **xxhash-x86dispatch** — DEAD. xxHash's runtime SIMD dispatcher, shipped alongside the amalgamation and never wired up: named in no CMake list and #included by nothing repo-wide. xxhash.h mentions it only in a doc comment. Retained pending the Director's decision to delete -- see #220.

<!-- END GENERATED: extern-provenance.py -->
