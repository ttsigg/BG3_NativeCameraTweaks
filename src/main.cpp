#include "Hooks.h"
#include "Settings.h"
#include "Utils.h"

#if !defined(_WIN32)
#	include <thread>
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
}
#endif
