# vcpkg overlay ports

Ports here SHADOW the registry ports of the same name, for this manifest only. Registered declaratively
in `source/vcpkg-configuration.json` (`overlay-ports`), so a clone bootstraps with no flag to remember.

That registration IS the point of this directory. These two ports previously lived outside the repository
and reached the build only through a hand-passed `-DVCPKG_OVERLAY_PORTS=...`, recorded in one developer's
CMakeCache. The patches were not missing from a pristine clone -- they were unreachable from it, which is
why a pristine Linux build could not bootstrap at all (#196).

An overlay is a FORK, and every fork is a carrying cost. Each one below therefore names the condition
under which it is DELETED, and says whether anything actually detects that condition.

## jemalloc -- retires ITSELF, enforced

DELTA: one `vcpkg_replace_string` rewriting jemalloc's `std::__throw_bad_alloc()` to `throw std::bad_alloc()`,
plus a `port-version` bump. Nothing else.

libstdc++ 16 removed the internal helper `std::__throw_bad_alloc()` from namespace `std`. jemalloc's C++
shim already includes `<new>`, so the standard spelling is a drop-in; clang uses libstdc++ here too, so
switching compiler does not avoid it.

RETIREMENT CONDITION: upstream jemalloc no longer calls `std::__throw_bad_alloc()`.

DETECTED BY: the port itself, fatally. `vcpkg_replace_string` compares the file hash before and after and
raises `Z_VCPKG_BACKCOMPAT_MESSAGE_LEVEL` when the substitution changed nothing -- and that level is
`FATAL_ERROR` (`scripts/ports.cmake`). The day upstream fixes this, the build STOPS and names this port.
No one has to remember to check.

## libsystemd -- retires on inspection, NOT enforced

DELTA: version 260.1 -> 260.2 (with the matching SHA512), and `-Dwerror=false` added to the meson options.
Reached transitively, not requested: `sdl3[dbus]` -> `dbus` -> `libsystemd`.

`-Dwerror=false` is the load-bearing half. systemd's build promotes warnings to errors, and the warnings it
emits under this toolchain are not ours to fix in a dependency we never asked for.

RETIREMENT CONDITION: the registry port reaches >= 260.2 AND builds here without `-Dwerror=false`.

DETECTED BY: **nothing**. Unlike jemalloc above, no mechanism fires when this becomes redundant -- a
stale version pin builds silently, and a redundant `werror=false` suppresses nothing and looks identical
to a necessary one. The check is manual, and its trigger is a baseline bump: after moving the
`default-registry` baseline in `source/vcpkg-configuration.json`, diff this port against the registry's
and delete it if the delta has become empty.
