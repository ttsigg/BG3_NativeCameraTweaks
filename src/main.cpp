#include "Hooks.h"
#include "Settings.h"
#include "Utils.h"

#if !defined(_WIN32)
#	include <thread>

#	include "CameraTweaksManager.h"
#	include "Linux/SdlInput.h"
#endif

namespace
{
	// Entry logic shared between the Windows DllMain path and the Linux
	// LD_PRELOAD constructor path.
	void Initialize()
	{
		// stuff
		dku::Logger::Init(Plugin::NAME, std::to_string(Plugin::Version));

		INFO("process : {}", dku::Hook::GetProcessName())
		const auto processPath = dku::Hook::GetProcessPath();
		INFO("process path : {}", processPath)

		std::string productVersion;
		if (Utils::GetProductVersion(processPath, productVersion)) {
			INFO("process version : {}", productVersion)
		} else {
			WARN("process version not found!")
		}

		const auto settings = Settings::Main::GetSingleton();
		settings->Load();

		if (*settings->WatchForConfigChanges) {
#if defined(_WIN32)
			_beginthreadex(NULL, 0, InitThread, NULL, 0, NULL);
#else
			std::thread([] { Settings::Main::GetSingleton()->WatchForChanges(); }).detach();
#endif
		}

		Hooks::Install();
	}
}

#if defined(_WIN32)
unsigned int __stdcall InitThread(void* param)
{
	const auto settings = Settings::Main::GetSingleton();
	if (*settings->WatchForConfigChanges) {
		settings->WatchForChanges();
	}

	return 0;
}

BOOL APIENTRY DllMain(HMODULE a_hModule, DWORD a_ul_reason_for_call, LPVOID a_lpReserved)
{
	if (a_ul_reason_for_call == DLL_PROCESS_ATTACH) {
#ifndef NDEBUG
		while (!IsDebuggerPresent()) {
			Sleep(100);
		}
#endif

		Initialize();
	}

	return TRUE;
}
#else
// Native-Linux entry: the mod is dlopen'd by bg3mods_preload.so, whose own
// gate has already confirmed this is the game process. The constructor runs
// during that dlopen, before the game's frame loop.
__attribute__((constructor)) static void BG3NCT_Init()
{
	Initialize();

	// SDL mouse delta (tracker 0x1441280d0-sdl-mouse-y, CONFIRMED obviated):
	// see src/Linux/SdlInput.h. Bind the sink before installing the watch —
	// InstallMouseYWatch() is idempotent, but a watch firing before a sink
	// is bound would just WARN-once and drop input. Must run after
	// Hooks::Install() (above) per SdlInput.h's contract.
	NCT::Linux::SetDeltaYSink([](int a_yrel) {
		// CameraTweaksManager.h's `delta_y` is a plain int, not
		// std::atomic (that header is outside this package's ownership),
		// and CalculateCameraPitch reads-and-zeroes it once per frame on
		// the game's own thread. This callback runs on whichever thread
		// SDL invokes event watches on, so it needs a real atomic
		// read-modify-write here rather than a plain `+=`.
		__atomic_fetch_add(&CameraTweaks::GetSingleton()->delta_y, a_yrel, __ATOMIC_RELAXED);
	});

	if (!NCT::Linux::InstallMouseYWatch()) {
		WARN("mouse-Y SDL watch not installed (see stderr) -- camera mouse-look pitch input will not work")
	}
}
#endif
