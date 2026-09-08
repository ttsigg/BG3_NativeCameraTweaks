#include "SdlInput.h"

// No PCH, no DKUtil, no Plugin.h — see the ownership note at the top of
// SdlInput.h. Everything this translation unit needs is standard C++ plus
// <dlfcn.h> for the runtime symbol lookup.
#include <atomic>
#include <cstdio>
#include <dlfcn.h>
#include <mutex>

namespace NCT::Linux
{
	namespace
	{
		// --- Minimal ABI mirror of SDL2's SDL_Event -----------------------
		//
		// We only ever read `type` and, for SDL_MOUSEMOTION, `motion.yrel`.
		// Pulling in the real <SDL_events.h> would mean pulling in the whole
		// SDL2 header tree (SDL_config.h and friends), which needs an -I
		// this package doesn't own the CMakeLists for
		// (BG3_NativeCameraTweaks/cmake/linux/CMakeLists.txt globs
		// src/Linux/*.cpp automatically but adds no SDL2 include path) —
		// so instead this mirrors just enough of the real layout by hand.
		//
		// Verified field-for-field against
		// Linux/sysroots/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper-sysroot/
		// usr/include/SDL2/SDL_events.h (SDL_MAJOR/MINOR/PATCHLEVEL = 2.32.70,
		// checked 2026-09-03) — SDL_MouseMotionEvent is 9 Uint32/Sint32
		// fields (36 bytes, no padding, all 4-byte aligned) and SDL_Event is
		// a union of every event struct padded to 56 bytes on the
		// sizeof(void*)<=8 (i.e. every 64-bit) branch. Both are ABI-frozen
		// SDL2 facts, not something that drifts per SDL2 point release.
		//
		// bg3mods_loader/test/sdl_watch_test.cpp links the REAL SDL2 headers
		// and library and static_asserts these two layouts against the real
		// SDL_MouseMotionEvent/SDL_Event, so any future ABI drift is caught
		// there rather than silently here.
		struct MinimalMouseMotionEvent
		{
			std::uint32_t type;       // SDL_MOUSEMOTION (0x400)
			std::uint32_t timestamp;
			std::uint32_t windowID;
			std::uint32_t which;
			std::uint32_t state;
			std::int32_t x;
			std::int32_t y;
			std::int32_t xrel;
			std::int32_t yrel;
		};
		static_assert(sizeof(MinimalMouseMotionEvent) == 36, "SDL_MouseMotionEvent layout drift");

		union MinimalEvent
		{
			std::uint32_t type;
			MinimalMouseMotionEvent motion;
			unsigned char padding[56];
		};
		static_assert(sizeof(MinimalEvent) == 56, "SDL_Event size drift (SDL2 64-bit ABI)");

		constexpr std::uint32_t kSDL_MOUSEMOTION = 0x400;

		// SDL_EventFilter's real signature is
		//   int (SDLCALL *)(void *userdata, SDL_Event *event)
		// SDLCALL is empty on every non-Windows target, i.e. plain SysV
		// cdecl — the same convention a `extern "C"` free function already
		// gets on Linux, so no extra decoration is needed for ABI
		// correctness. `extern "C"` is used anyway for linkage clarity.
		using PFN_SDL_AddEventWatch = void (*)(int (*a_filter)(void*, MinimalEvent*), void* a_userdata);
		using PFN_SDL_DelEventWatch = void (*)(int (*a_filter)(void*, MinimalEvent*), void* a_userdata);

		std::atomic<DeltaYSink> g_sink{ nullptr };
		std::atomic<bool> g_installed{ false };

		// Only ever written by InstallMouseYWatch(), and only ever read by
		// RemoveMouseYWatch() (or the thunk) after observing g_installed ==
		// true — which happens-after that write via g_installed's
		// release/acquire pair below — so no separate synchronization is
		// needed for these two.
		PFN_SDL_AddEventWatch g_addEventWatch = nullptr;
		PFN_SDL_DelEventWatch g_delEventWatch = nullptr;

		std::once_flag g_warnSdlMissingFlag;
		std::once_flag g_warnNoSinkFlag;

		void WarnOnce(std::once_flag& a_flag, const char* a_message)
		{
			std::call_once(a_flag, [a_message] {
				std::fprintf(stderr, "[BG3NativeCameraTweaks][WARN] %s\n", a_message);
			});
		}

		// The watch callback itself. Return value is ignored by SDL for
		// watches (unlike filters, where 0 discards the event) — returned
		// as 1 purely for readability.
		extern "C" int MouseYWatchThunk(void* /*a_userdata*/, MinimalEvent* a_event)
		{
			if (a_event && a_event->type == kSDL_MOUSEMOTION) {
				if (const auto sink = g_sink.load(std::memory_order_acquire)) {
					sink(a_event->motion.yrel);
				} else {
					WarnOnce(g_warnNoSinkFlag,
						"NCT::Linux mouse-Y watch fired with no delta_y sink bound "
						"(SetDeltaYSink() was never called) -- mouse pitch input is being dropped");
				}
			}
			return 1;
		}
	}  // namespace

	void SetDeltaYSink(DeltaYSink a_sink) noexcept
	{
		g_sink.store(a_sink, std::memory_order_release);
	}

	bool InstallMouseYWatch()
	{
		if (g_installed.load(std::memory_order_acquire)) {
			return true;  // idempotent
		}

		// RTLD_DEFAULT: search the global scope (the executable and every
		// already-loaded shared object, including the game's own libSDL2),
		// not "the next object after this one" (that's RTLD_NEXT, meant for
		// interposing a symbol of the SAME name this TU also defines, which
		// isn't the case here).
		auto* const addFn = reinterpret_cast<PFN_SDL_AddEventWatch>(dlsym(RTLD_DEFAULT, "SDL_AddEventWatch"));
		auto* const delFn = reinterpret_cast<PFN_SDL_DelEventWatch>(dlsym(RTLD_DEFAULT, "SDL_DelEventWatch"));
		if (!addFn || !delFn) {
			WarnOnce(g_warnSdlMissingFlag,
				"SDL2 is not loaded in this process yet (dlsym(SDL_AddEventWatch/SDL_DelEventWatch) "
				"found nothing) -- mouse-Y watch not installed, mouse pitch input will not work");
			return false;
		}

		g_addEventWatch = addFn;
		g_delEventWatch = delFn;
		g_addEventWatch(&MouseYWatchThunk, nullptr);
		g_installed.store(true, std::memory_order_release);
		return true;
	}

	void RemoveMouseYWatch()
	{
		if (!g_installed.exchange(false, std::memory_order_acq_rel)) {
			return;  // never installed, or already removed
		}
		if (g_delEventWatch) {
			g_delEventWatch(&MouseYWatchThunk, nullptr);
		}
	}

}  // namespace NCT::Linux
