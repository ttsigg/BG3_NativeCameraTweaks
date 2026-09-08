#include "Utils.h"

#include "Hooks.h"

#if defined(_WIN32)
#pragma comment(lib, "version")
#else
#	include "Linux/LinuxLayout.h"
#endif

namespace Utils
{
    bool GetProductVersion(std::string_view a_processPath, std::string& a_outProductVersion)
    {
#if defined(_WIN32)
		const auto file = fmt::detail::utf8_to_utf16(a_processPath);
		DWORD dwHandle;
		const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &dwHandle);
		if (size != 0) {
			std::vector<BYTE> versionInfoBuffer(size);

			if (GetFileVersionInfoW(file.c_str(), 0, size, versionInfoBuffer.data())) {
				LPVOID versionInfo;
				UINT versionInfoSize;
				if (VerQueryValueW(versionInfoBuffer.data(), L"\\StringFileInfo\\000004B0\\ProductVersion", &versionInfo, &versionInfoSize)) {
					const auto productVersion = fmt::detail::to_utf8<wchar_t>(static_cast<const wchar_t*>(versionInfo));
					a_outProductVersion = productVersion.c_str();

					return true;
				}
			}
		}

		return false;
#else
		// ELF has no VERSIONINFO resource, but it does carry an identity: the
		// NT_GNU_BUILD_ID note, which changes on every relink and is therefore a
		// sharper build fingerprint than a product-version string. DKUtil reads it
		// out of the running program's PT_NOTE for the site catalog's build gate
		// (SiteCatalog.hpp); report the same value here so the mod's own log
		// records which build it attached to — without it a stale-catalog report
		// has nothing to compare against.
		(void)a_processPath;
		const auto& buildId = dku::Hook::SiteCatalog::running_build_id();
		if (buildId.empty()) {
			return false;
		}
		a_outProductVersion = "GNU build-id " + buildId;
		return true;
#endif
    }

    int16_t GetPlayerID(RE::UnkObject* a1)
    {
		auto currentPlayer = Utils::GetCurrentPlayer(a1);
		if (!currentPlayer) {
			return 1;
		}

		return currentPlayer->playerId_38;
	}

	RE::Player* GetCurrentPlayer(RE::UnkObject* a1)
	{
#if defined(_WIN32)
		return a1->currentPlayer;
#else
		// RE::UnkObject is member-reordered on Linux (tracker 0x141e0f120 §3.2):
		// currentPlayer is Win +0x10 -> Lin +0x20, dual-confirmed. Reading the
		// C++ struct field here would read Linux +0x10, a different pool slot.
		if (!a1 || !NCT::LinuxLayout::Known(NCT::LinuxLayout::kUnkObject_currentPlayer)) {
			return nullptr;
		}
		return *reinterpret_cast<RE::Player**>(
			reinterpret_cast<uintptr_t>(a1) + NCT::LinuxLayout::kUnkObject_currentPlayer);
#endif
	}

	RE::CameraDefinition* GetCurrentCameraDefinition(RE::CameraModeFlags a_cameraModeFlags)
	{
#if !defined(_WIN32)
		// On Linux this is not a fallback: the game function is inlined at every
		// call site (tracker 0x141c714e0-get-current-camera-definition, review
		// 2026-09-01 CONFIRMED — 19 hosts, no standalone counterpart exists), so
		// Hooks::Offsets::GetCurrentCameraDefinition is permanently null and this
		// reimplementation is the ONLY path. The four offsets it uses come from
		// LinuxLayout.h (0x1362 / 0xC80 / 0x7C4 / 0x958).
		if (!Hooks::Offsets::UnkCameraSingletonPtr || !*Hooks::Offsets::UnkCameraSingletonPtr) {
			return nullptr;
		}
#endif
		// replicated inlined game function.
		// NOTE: the original read `reinterpret_cast<bool>(ptr) + offset`, which casts
		// the singleton POINTER to bool (always true when non-null) rather than
		// reading the bool AT singleton+cameraBoolOffset. That is a pre-existing
		// upstream bug (this whole function is dead code — live code calls
		// Offsets::GetCurrentCameraDefinition). Fixed here to the intended read so it
		// compiles under gcc/clang; the offsets still need Linux re-validation.
		if (*reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::cameraBoolOffset)) {
			return reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::unkCameraOffset);
		} else if ((a_cameraModeFlags & 1) == 0) {
			return reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::explorationCameraOffset);
		}

		return reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::combatCameraOffset);
	}
}
