# Vendored provenance: source/extern

Generated from `docs/architecture/extern-provenance.json` by `scripts/extern-provenance.py`.
Do not edit the block below; edit the register and re-inject. `run-gates.sh` fails on drift.

<!-- BEGIN GENERATED: scripts/extern-provenance.py -->
<!-- Regenerate with: python3 scripts/extern-provenance.py --inject -->

| artefact | version | reach | licence | upstream | attributed |
|---|---|---|---|---|---|
| `curve25519` | unversioned-vendor-drop | direct | MIT | https://github.com/msotoodeh/curve25519 | doc/OPENSOURCE.md |
| `fast_float` | unversioned-vendor-drop | header | Apache-2.0 OR MIT OR BSL-1.0 | https://github.com/fastfloat/fast_float | doc/OPENSOURCE.md |
| `fmt` | 10.2.1 | direct | MIT | https://github.com/fmtlib/fmt | doc/OPENSOURCE.md |
| `imgui_lua_bindings` | unversioned-vendor-drop | direct | No formal licence; permissive grant in upstream README | https://github.com/patrickriordan/imgui_lua_bindings | doc/OPENSOURCE.md |
| `lua` | 5.3.6 | direct | MIT | https://www.lua.org/ | doc/OPENSOURCE.md |
| `rpmalloc` | unversioned-vendor-drop | conditional (STAR_USE_RPMALLOC) | Public Domain | https://github.com/mjansson/rpmalloc | doc/OPENSOURCE.md |
| `xxhash` | 0.8.1 | direct | BSD-2-Clause | https://github.com/Cyan4973/xxHash | doc/OPENSOURCE.md |

### Why each is vendored

- **curve25519** — Ed25519 signing and X25519 key exchange for the network handshake. No release tags in the vendored drop, so no version is derivable and none is claimed.
- **fast_float** — Locale-independent string-to-float used by the Json parser. Header-only single file; the vendored copy carries no version macro. Tri-licensed upstream; we take the MIT option and say so in the attribution.
- **fmt** — Formatting for strf/Logger across the whole tree. Vendored rather than packaged so the format-string ABI cannot shift under a vcpkg baseline bump mid-campaign.
- **imgui_lua_bindings** — Exposes the debug ImGui surface to Lua. The vendored copy carries no licence header and upstream publishes no LICENCE file, but the upstream README DOES grant permission in informal terms -- explicitly including use in an open source project. That grant is quoted verbatim in the attribution rather than paraphrased, and it imposes one obligation silence would breach: not claiming the code as ours.
- **lua** — The scripting language the entire mod surface is written against. Vendored by upstream Starbound; the interpreter is not swappable for a packaged one without changing script semantics.
- **rpmalloc** — Optional allocator, one of three (jemalloc / mimalloc / rpmalloc). malloc.c is the operator-new shim and is NOT in any CMake list -- rpmalloc.c #includes it, so it compiles only through that.
- **xxhash** — Content hashing for the render caches and the harness frame hash. xxh3.h is reached from source/core/StarXXHash.hpp via the include path, NOT through CMakeLists -- it is live despite being named in no build list.

<!-- END GENERATED: extern-provenance.py -->
