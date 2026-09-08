// Compiles the checked-in src/RE/Generated/LayoutManifest.inl against the
// current RE/ headers. A stale manifest -- one that no longer matches what
// this toolchain actually lays these structs out as -- is a compile error
// here, not a silent misread of live game memory later.
//
// See docs/linux-port/BUILDING.md "The layout harness" (NCT section) for how
// to regenerate this manifest, and docs/linux-port/tools/gen_layout_manifest.py
// for what it does and does not prove.
//
// This TU IS part of the real Linux build: cmake/linux/CMakeLists.txt globs
// src/RE/*.cpp into the shipped target precisely so these static_asserts run on
// every build. It is additionally compiled standalone by
// docs/linux-port/tools/test_layout_harness.py's
// test_nct_injected_field_breaks_the_checked_in_manifest, with the same flags
// (nct_layout_flags.txt). Do not add it to the target explicitly -- the glob
// already has it, and a second entry is a duplicate-source error.
#include "RE/Camera.h"
#include "RE/Generated/LayoutManifest.inl"
