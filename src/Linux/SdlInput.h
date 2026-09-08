#pragma once

// Linux replacement for the Windows Hook_SDLMouseYHook call-site hook (see
// ../Hooks.cpp "SDL mouse delta" and tracker 0x1441280d0-sdl-mouse-y —
// CONFIRMED obviated). Windows hooks a call site inside bg3.exe with an
// Xbyak stub that feeds the raw mouse-Y delta (a_deltaY) into
// CameraTweaks::delta_y. There is no Linux counterpart call site to hook, so
// instead this registers an SDL_AddEventWatch callback and reads
// SDL_MOUSEMOTION.yrel directly off the same event stream the game's own
// SDL backend produces — the value is equivalent to the Windows a_deltaY.
//
// Deliberately does NOT link libSDL2 at build time. The game process already
// has it loaded (confirmed exports:
//   nm -D --defined-only Linux/libSDL2.so | grep -i eventwatch
//     000000000009a050 T SDL_AddEventWatch
//     000000000009a060 T SDL_DelEventWatch
// ), so InstallMouseYWatch() resolves both entry points with
// dlsym(RTLD_DEFAULT, ...) at call time instead of importing them — the same
// "find it in the host process, don't link it" approach the rest of this mod
// takes to game-owned code via the site catalog.
//
// This header and SdlInput.cpp intentionally take NO dependency on the
// mod's PCH.h, DKUtil, or the generated Plugin.h: bg3mods_loader/test/
// sdl_watch_test.cpp compiles SdlInput.cpp directly as a second source file
// in a standalone test executable that has none of those on its include
// path (see bg3mods_loader/test/sdl_watch.cmake). Only <cstdint> here.

#include <cstdint>

namespace NCT::Linux
{
	// Invoked once per SDL_MOUSEMOTION event, on whatever thread SDL invokes
	// its event-watch callbacks on (measured in bg3mods_loader/test/
	// sdl_watch_test.cpp: the thread that calls SDL_PumpEvents / SDL_PollEvent
	// / SDL_PushEvent; the shipped game may pump on a dedicated input
	// thread), carrying that event's `yrel`.
	//
	// main.cpp binds this to CameraTweaks::delta_y.fetch_add(). The camera
	// hook consumes and clears that atomic with exchange(), so no motion is
	// lost between a separate SDL input thread and the game thread.
	using DeltaYSink = void (*)(int a_yrel);

	// Binds (or rebinds) the sink invoked by the mouse-Y watch. Must be
	// called before InstallMouseYWatch() — a watch that fires with no sink
	// bound WARN-onces to stderr and drops the event. Not itself
	// synchronized against a concurrently-running watch callback; call it
	// once, early, from the same thread that calls InstallMouseYWatch()
	// (main.cpp's single-threaded Linux constructor path does exactly
	// that).
	void SetDeltaYSink(DeltaYSink a_sink) noexcept;

	// Resolves SDL_AddEventWatch via dlsym(RTLD_DEFAULT, ...) and registers
	// the watch. Idempotent (a second call while already installed just
	// returns true). Never throws.
	//
	// Returns false, after a single WARN-once to stderr, if SDL2 is not yet
	// loaded anywhere in this process (dlsym finds neither
	// SDL_AddEventWatch nor SDL_DelEventWatch). Call from main.cpp's Linux
	// constructor path, after Hooks::Install() has run.
	bool InstallMouseYWatch();

	// Resolves SDL_DelEventWatch via dlsym(RTLD_DEFAULT, ...) and removes a
	// previously installed watch. No-op (does not re-attempt dlsym) if no
	// watch is currently installed. Never throws.
	void RemoveMouseYWatch();

}  // namespace NCT::Linux
