# BG3_NativeCameraTweaks — Linux build

Standalone CMake project that builds `libBG3NativeCameraTweaks.so` from the
mod's existing sources (`../../src`). It is deliberately **not** wired into
`../../CMakeLists.txt` — that project stays untouched for the Windows/MSVC/
vcpkg build. This one reads the same `.cpp`/`.h` files but does everything
else the Linux way: no vcpkg, no PowerShell source-gen, headers found via the
top-level DKUtil fork and `Linux/deps/` instead of `find_package(... CONFIG
REQUIRED)`.

See the top-level `AGENTS.md` and `docs/linux-port/DKUTIL-PORT-DESIGN.md` for
the wider port. This file covers only what's needed to build this one target.

## 1. Fetch the header-only dependencies (once)

```bash
docs/linux-port/tools/fetch_deps.sh
```

Idempotent — pulls pinned versions of spdlog, xbyak, simpleini, tomlplusplus
(git, shallow clone, tag verified against a pinned commit SHA) and the
nlohmann/json single header (raw download, verified against a pinned SHA256)
into `Linux/deps/`. Safe to re-run; add `--force` to replace an existing copy.
`Linux/deps/` is not tracked by any of the workspace's git checkouts — same
policy as the rest of `Linux/`.

## 2. In-container build (compile/ABI check)

```bash
cmake -B BG3_NativeCameraTweaks/build-linux -G Ninja \
    -S BG3_NativeCameraTweaks/cmake/linux \
    -DCMAKE_CXX_COMPILER=g++
cmake --build BG3_NativeCameraTweaks/build-linux
```

Produces `BG3_NativeCameraTweaks/build-linux/libBG3NativeCameraTweaks.so`.
Verify it has no undefined game/Windows symbols:

```bash
nm -D --undefined-only BG3_NativeCameraTweaks/build-linux/libBG3NativeCameraTweaks.so \
  | c++filt | grep -viE '@GLIBC|@GLIBCXX|@CXXABI|@GCC_|^\s*w '
# empty output == clean (every undefined symbol resolves to libc/libstdc++/libgcc)
```

**This build is a compile/ABI check only.** The rootless container can't run
pressure-vessel, and this `.so` links against the *host's* glibc/libstdc++
(g++ 16), which is newer than what BG3 runs inside (Steam Runtime 3 sniper:
glibc 2.31 / GLIBCXX_3.4.28) — it will not load in the actual game. It's the
right inner loop for iterating on the CMake/build plumbing and catching
compile errors early, though; see `docs/linux-port/BUILDING.md` "ABI: host
builds are NOT shippable" for the measured evidence.

## 3. Host-side build against the sniper SDK (the shippable artifact)

Follow `docs/linux-port/BUILDING.md` → "Building against the sniper SDK" to
unpack and relativize the sysroot once, then:

```bash
SR="$(pwd)/Linux/sysroots/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper-sysroot"

cmake -B BG3_NativeCameraTweaks/build-linux-sniper -G Ninja \
    -S BG3_NativeCameraTweaks/cmake/linux \
    --toolchain "$(pwd)/bg3se/cmake/toolchains/steamrt-sniper.cmake" \
    -DBG3SE_SNIPER_SYSROOT="$SR"
cmake --build BG3_NativeCameraTweaks/build-linux-sniper
```

(Pass absolute paths to `--toolchain` and `-DBG3SE_SNIPER_SYSROOT` — CMake
resolves a relative `--toolchain` against the build directory, not your cwd.)

This is the **only** toolchain to trust for a `.so` that actually loads
inside the game: SteamRT's `gcc-14` links libstdc++ statically, so the
artifact carries no `GLIBCXX` dependency at all (see BUILDING.md for the
measured before/after).

### ⚠ Known gap: the sniper SDK's gcc-14 lacks some C++23 library features DKUtil's Linux port uses

Measured 2026-09-03, `cmake --build BG3_NativeCameraTweaks/build-linux-sniper`
(compiler resolved correctly to the SDK's `gcc-14.2.0` via the wrapper — the
toolchain hook itself works) fails compiling `DKUtil/include/DKUtil/Impl/
Utility/string.hpp` under `-std=gnu++23`:

```
error: 'starts_with' is not a member of 'std::ranges'
error: 'class std::__cxx11::basic_string<char>' has no member named 'append_range'
```

This is a **pre-existing DKUtil-fork / toolchain-floor gap**, not something in
this mod's own sources or this CMakeLists — `docs/linux-port/DKUTIL-PORT-
DESIGN.md` §7 already flags it ("Toolchain floor: the periphery uses
`ranges::to`, `views::join_with`, `std::unreachable`, consteval
`static_string`. Needs gcc ≥14 / clang ≥18 **with a recent libstdc++**").
The SDK's bundled `gcc-14.2.0` apparently ships a libstdc++ that hasn't
finished implementing the P1659/P2495 range algorithms/`append_range`
DKUtil's `string.hpp` calls, even though the *language* accepts `-std=c++23`.
Not owned by this package (`DKUtil/` is a separate top-level checkout) — filed
here rather than patched. Until it's resolved upstream in the DKUtil fork, the
in-container build (§2) is the one that actually exercises this mod's own
sources end-to-end; the sniper build's *plumbing* (toolchain selection,
sysroot, wrapper scripts, include order) is verified working up to that point.

## Variables

| Var | Default | Meaning |
|---|---|---|
| `DKUTIL_ROOT` | `<workspace>/DKUtil` | The Linux DKUtil fork (`ttsigg/DKUtil`, branch `linux-port`). Since 2026-09-08 `BG3_NativeCameraTweaks/extern/DKUtil` is the *same* fork at the same commit (`797ae07`), so either path works — but the default stays the top-level checkout, which is the one the fork is developed in. If the two ever disagree, the top-level one wins. |
| `DEPS_ROOT` | `<workspace>/Linux/deps` | Output of `fetch_deps.sh`. |

Both are configure-time checks — the project fails fast with a clear message
naming the missing header if either is wrong, rather than a wall of `#include`
errors from deep inside DKUtil.

## Notes

- **Source list**: globbed at configure time from `../../src/*.cpp` plus
  `../../src/Linux/*.cpp` if that directory exists (it doesn't yet — mirrors
  the MSVC project's PowerShell `!Update.ps1 SOURCEGEN` step, which this
  replaces per `DKUTIL-PORT-DESIGN.md` §8). `CONFIGURE_DEPENDS` re-globs on
  build, so a source file added later needs no CMakeLists edit.
- **`include/Plugin.h`** is configured from `../Plugin.h.in` at configure
  time (mirrors `../../CMakeLists.txt`'s own `configure_file` step) rather
  than reusing the checked-in `../../src/Plugin.h` fallback, so version bumps
  stay in one place (this file's `PROJECT_VERSION_*` block) instead of two.
- **Include order is load-bearing**: DKUtil (+ its bundled `external/toml.hpp`)
  first, then `Linux/deps/*`, then the mod's own headers — so nothing fetched
  into `Linux/deps` can shadow a name DKUtil expects to resolve first-party.
- `tomlplusplus` is on the include path per the fetch contract but isn't
  actually consumed here: DKUtil vendors its own `toml++ 3.4.0` at
  `DKUtil/include/external/toml.hpp` and includes that, not the fetched copy.
- Output is `lib` + `BG3NativeCameraTweaks` + `.so`, i.e.
  `libBG3NativeCameraTweaks.so` — matches what `bg3mods_loader`'s
  `NativeMods/*.so` discovery and its `nct_loads_with_catalog` ctest expect.
