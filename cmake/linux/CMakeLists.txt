cmake_minimum_required(VERSION 3.21)

# Standalone Linux build of BG3_NativeCameraTweaks — libBG3NativeCameraTweaks.so.
#
# Deliberately NOT a subdirectory of the mod's own (MSVC/vcpkg) CMakeLists.txt:
# that one is untouched for the Windows build (see ../../CMakeLists.txt). This
# project reads the same sources (../../src) but wires them up the Linux way —
# no vcpkg, no powershell source-gen, headers found under Linux/deps/ and the
# top-level DKUtil fork instead of find_package(... CONFIG REQUIRED).
#
# Two ways to build this:
#
#   In-container (compile/ABI check only — see docs/linux-port/BUILDING.md
#   "ABI: host builds are NOT shippable"):
#     cmake -B build-linux -G Ninja \
#         -S BG3_NativeCameraTweaks/cmake/linux
#     cmake --build build-linux
#
#   Host-side against the Steam sniper SDK (the only SHIPPABLE artifact):
#     cmake -B build-linux-sniper -G Ninja \
#         -S BG3_NativeCameraTweaks/cmake/linux \
#         --toolchain <workspace>/bg3se/cmake/toolchains/steamrt-sniper.cmake \
#         -DBG3SE_SNIPER_SYSROOT=<unpacked sniper sysroot>
#     cmake --build build-linux-sniper
#
# See ./README.md for the prerequisite `fetch_deps.sh` step and details.

project(BG3NativeCameraTweaksLinux LANGUAGES CXX)

if(WIN32)
	message(FATAL_ERROR
		"BG3_NativeCameraTweaks/cmake/linux is the Linux build. Use the "
		"MSVC/vcpkg project at BG3_NativeCameraTweaks/CMakeLists.txt on Windows.")
endif()

if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_BINARY_DIR})
	message(FATAL_ERROR "In-source builds are not allowed.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- layout ------------------------------------------------------------
# This file lives at <mod>/cmake/linux/CMakeLists.txt.
set(NCT_ROOT       "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(NCT_ROOT "${NCT_ROOT}" ABSOLUTE)
set(NCT_SRC_DIR    "${NCT_ROOT}/src")

# Workspace root: <mod>/../.. — same place fetch_deps.sh, DKUtil/ and Linux/
# live, matching the umbrella layout documented in the top-level CLAUDE.md.
set(WORKSPACE_ROOT "${NCT_ROOT}/..")
get_filename_component(WORKSPACE_ROOT "${WORKSPACE_ROOT}" ABSOLUTE)

set(DKUTIL_ROOT "${WORKSPACE_ROOT}/DKUtil"    CACHE PATH "Path to the Linux DKUtil fork (branch linux-port)")
set(DEPS_ROOT   "${WORKSPACE_ROOT}/Linux/deps" CACHE PATH "Path to fetch_deps.sh's output directory")

if(NOT EXISTS "${DKUTIL_ROOT}/include/DKUtil/Hook.hpp")
	message(FATAL_ERROR
		"DKUtil fork not found at ${DKUTIL_ROOT} (expected include/DKUtil/Hook.hpp). "
		"This must be the Linux DKUtil fork (ttsigg/DKUtil, branch linux-port) -- either "
		"the top-level workspace DKUtil/ (the default, and where the fork is developed) or "
		"BG3_NativeCameraTweaks/extern/DKUtil, which has pointed at the same fork since "
		"2026-09-08. Run 'git submodule update --init' if the submodule is empty. "
		"Pass -DDKUTIL_ROOT=<path> to override.")
endif()

foreach(dep_hdr
	"spdlog/include/spdlog/spdlog.h"
	"xbyak/xbyak/xbyak.h"
	"simpleini/SimpleIni.h"
	"nlohmann/json.hpp")
	if(NOT EXISTS "${DEPS_ROOT}/${dep_hdr}")
		message(FATAL_ERROR
			"Dependency header ${dep_hdr} not found under ${DEPS_ROOT}. "
			"Run docs/linux-port/tools/fetch_deps.sh first (see cmake/linux/README.md). "
			"Pass -DDEPS_ROOT=<path> to override.")
	endif()
endforeach()

# --- sources -------------------------------------------------------------
# Mirrors the MSVC project's powershell source-gen (!Update.ps1 SOURCEGEN)
# with a configure-time glob instead, per DKUTIL-PORT-DESIGN.md sec 8. A
# src/Linux/*.cpp directory doesn't exist yet but is picked up automatically
# once it does — re-run cmake (or just build; CONFIGURE_DEPENDS re-globs).
file(GLOB NCT_SOURCES CONFIGURE_DEPENDS "${NCT_SRC_DIR}/*.cpp")
file(GLOB NCT_LINUX_SOURCES CONFIGURE_DEPENDS "${NCT_SRC_DIR}/Linux/*.cpp")
list(APPEND NCT_SOURCES ${NCT_LINUX_SOURCES})

# src/RE/*.cpp is deliberately included: RE/LayoutCheck.cpp compiles the checked-in
# RE/Generated/LayoutManifest.inl, whose 235 static_asserts are the only guard
# against these mirrored struct layouts silently shifting under a header edit or a
# compiler change. Without it the manifest is only exercised by
# docs/linux-port/tools/test_layout_harness.py and the shipped .so is built with
# no layout check at all -- while Hook_SetDefaultZoom writes floats into live game
# memory at RE::CameraObject offsets. See docs/linux-port/BUILDING.md
# "The layout harness".
file(GLOB NCT_RE_SOURCES CONFIGURE_DEPENDS "${NCT_SRC_DIR}/RE/*.cpp")
list(APPEND NCT_SOURCES ${NCT_RE_SOURCES})

if(NOT NCT_SOURCES)
	message(FATAL_ERROR "No sources found under ${NCT_SRC_DIR}")
endif()

# --- configured Plugin.h --------------------------------------------------
# Mirrors ../../CMakeLists.txt's configure_file(cmake/Plugin.h.in ...). Reads
# the same project metadata (version, name) the MSVC build's Plugin.h.in
# uses; PluginAuthor isn't a real cache var upstream either (blank there too,
# see the checked-in src/Plugin.h fallback), so leave it blank here as well.
set(PROJECT_VERSION_MAJOR 2)
set(PROJECT_VERSION_MINOR 4)
set(PROJECT_VERSION_PATCH 5)
set(PluginAuthor "")
# Deliberately overwrites the top-level project(BG3NativeCameraTweaksLinux)
# name in this scope: Plugin.h.in's @PROJECT_NAME@ token must resolve to the
# mod's own name (matching Windows), not this listfile's CMake project name.
set(PROJECT_NAME "BG3NativeCameraTweaks")

configure_file(
	"${NCT_ROOT}/cmake/Plugin.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/Plugin.h"
	@ONLY
)

# --- target ----------------------------------------------------------------
add_library(BG3NativeCameraTweaks SHARED ${NCT_SOURCES})

target_compile_options(BG3NativeCameraTweaks PRIVATE
	-fPIC
	-fvisibility=hidden
	-include "${NCT_SRC_DIR}/PCH.h"
)

target_compile_definitions(BG3NativeCameraTweaks PRIVATE
	SPDLOG_HEADER_ONLY
	PLUGIN_MODE
	# The Steam sniper SDK's gcc-14 libstdc++ auto-enables its TBB-backed
	# Parallel STL the moment /usr/include/tbb/tbb.h is visible on the
	# sysroot include path (bits/c++config.h: "#define
	# _GLIBCXX_USE_TBB_PAR_BACKEND __has_include(<tbb/tbb.h>)", unguarded by
	# anything this project controls). The sniper sysroot ships those
	# headers but nothing here links -ltbb, so any TU that transitively
	# includes <algorithm> compiles clean but leaves an undefined reference
	# to tbb::interface7::internal::task_arena_base::internal_current_slot()
	# in the linked .so. Must be a command-line -D (bits/c++config.h's own
	# include guard latches the default before any of our headers run).
	# Harmless on a non-sniper build: it only disables an opt-in parallel
	# backend nothing here requests. See DKUtil/test-linux/CMakeLists.txt
	# for the same fix applied there first.
	_GLIBCXX_USE_TBB_PAR_BACKEND=0
)

# Include order is load-bearing: DKUtil (+ its bundled toml++) first, then
# the fetched deps, then the mod's own configured/src headers — so a name
# DKUtil expects to find first-party (e.g. its vendored external/toml.hpp)
# never resolves against a same-named file fetched into Linux/deps instead.
target_include_directories(BG3NativeCameraTweaks PRIVATE
	"${DKUTIL_ROOT}/include"
	"${DKUTIL_ROOT}/include/external"
	"${DEPS_ROOT}/spdlog/include"
	"${DEPS_ROOT}/xbyak"
	"${DEPS_ROOT}/simpleini"
	"${DEPS_ROOT}"                       # nlohmann/json.hpp -> <nlohmann/json.hpp>
	"${DEPS_ROOT}/tomlplusplus/include"  # unused by DKUtil (vendors its own toml++)
	                                      # but kept on the path per the deps contract
	"${CMAKE_CURRENT_BINARY_DIR}/include" # configured Plugin.h
	"${NCT_SRC_DIR}"
)

target_link_libraries(BG3NativeCameraTweaks PRIVATE dl pthread)

# --- keep the statically-linked C++ runtime PRIVATE -----------------------
# SteamRT's gcc-14 links libstdc++/libsupc++/libgcc STATICALLY (see
# bg3se/cmake/toolchains/steamrt-sniper.cmake), which is what lets the shipped
# .so carry no GLIBCXX dependency at all. But -fvisibility=hidden above only
# covers THIS project's translation units; symbols pulled in from those
# archives keep their default visibility and land in .dynsym. The loader
# dlopen()s the mod RTLD_GLOBAL (bg3mods_loader/bg3mods_preload.cpp) into a
# process that already has libstdc++.so.6 mapped (a DT_NEEDED of bg3's own
# libBink2x64.so / libnvsdk_ngx.so), so without this the mod would export
# ~7.5k std/__cxa symbols and its own std calls could bind back to the
# process's older libstdc++ -- half the runtime from each copy, e.g. locale
# facet vtables from one and facet id tables from the other. That defeats the
# entire point of static linking.
#
# --exclude-libs,ALL localises every symbol that came out of a static archive
# and nothing else. Nothing this mod exports on purpose lives in an archive:
# the loader never dlsym()s it (bg3mods_preload.cpp just dlopens and lets the
# constructor run), and anything that did want exporting would carry PCH.h's
# DLLEXPORT (__attribute__((visibility("default")))) from a TU compiled right
# here. Measured on the sniper .so: dynamic definitions fell 8986 -> 2648 and
# `T` definitions 1519 -> 0; _Znwm / _ZdlPv / _ZdlPvm / __cxa_throw /
# __cxa_allocate_exception / __cxa_get_globals / __gxx_personality_v0 are all
# gone. Re-check with:
#   nm -D --defined-only libBG3NativeCameraTweaks.so | awk '$2=="T"'   # empty
# What remains is weak (W) template instantiations from this project's own
# objects: libstdc++ declares `namespace std _GLIBCXX_VISIBILITY(default)`, so
# -fvisibility=hidden cannot hide those. They are inline-identical to any other
# copy in the process, so vague-linkage merging across them is benign -- unlike
# the runtime's own out-of-line state (allocator, EH, locale facet ids), which
# is what this flag keeps private.
target_link_options(BG3NativeCameraTweaks PRIVATE
	"LINKER:--exclude-libs,ALL"
)

set_target_properties(BG3NativeCameraTweaks PROPERTIES
	OUTPUT_NAME "BG3NativeCameraTweaks"
	PREFIX "lib"
)

# --- toolchain hook ----------------------------------------------------
# Nothing project-specific needed here: passing
#   --toolchain <workspace>/bg3se/cmake/toolchains/steamrt-sniper.cmake
#   -DBG3SE_SNIPER_SYSROOT=<sysroot>
# on the cmake command line is enough — CMake applies a --toolchain file
# before this listfile runs, so the sniper gcc-14 + sysroot are already
# selected by the time add_library() above executes. See
# docs/linux-port/BUILDING.md "Building against the sniper SDK" and
# ./README.md.
