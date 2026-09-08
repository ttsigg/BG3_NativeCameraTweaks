#pragma once
#include "RE/Camera.h"

#if !defined(_WIN32)
#	include "Linux/LinuxLayout.h"
#endif

namespace Hooks
{
	using namespace DKUtil::Alias;

#if !defined(_WIN32)
	// Site-catalog keys.
	//
	// On Linux `search_pattern<"...">()` does not scan bytes — DKUtil rewrites it
	// into `SiteCatalog::resolve(<the literal>)` (DKUtil/Impl/Hook/Assembly.hpp).
	// We look the same strings up directly so we can also inspect `kind`/`relation`
	// before using an address. EVERY string below must stay byte-identical to the
	// `search_pattern<...>` literal in the Windows branch it mirrors, because that
	// literal is the catalog key (docs/linux-port/data/site-catalog.json).
	//
	// "NCT:<name>" keys are synthetic: the RE pipeline mints them for values NCT
	// used to derive by walking bytes inside a resolved function, which cannot be
	// done against clang output.
	namespace Keys
	{
		using namespace std::string_view_literals;

		// --- Offsets::Init ---
		inline constexpr auto GlobalSwitches = "48 8B 05 ?? ?? ?? ?? 80 B8 32 13 00 00 00 74 07"sv;
		inline constexpr auto UnkSingletonFn = "48 8B 0D ?? ?? ?? ?? 0F B7 D0 E8 ?? ?? ?? ?? 84 C0 75 1B"sv;
		inline constexpr auto PlayerControllerFn = "48 8B 0D ?? ?? ?? ?? 0F B7 D7 E8 ?? ?? ?? ?? 3C FF"sv;
		inline constexpr auto IsInControllerMode = "80 3D ?? ?? ?? ?? ?? 74 22"sv;
		inline constexpr auto GetCharacter = "E8 ?? ?? ?? ?? 41 0F 28 45 40"sv;
		inline constexpr auto GetCharacterHeight = "48 89 5C 24 08 57 48 83 EC 30 48 8B 41 10 48 8B D9"sv;
		inline constexpr auto GetFloorLevel = "E8 ?? ?? ?? ?? EB 11 33 C9"sv;
		inline constexpr auto GetCameraMinZoom = "E8 ?? ?? ?? ?? 0F 28 DA F3 0F 5C E5"sv;

		// Synthetic keys (see the CATALOG CONTRACT).
		inline constexpr auto gInputManager = "NCT:gInputManager"sv;
		inline constexpr auto UnkPlayerSingletonPtr = "NCT:UnkPlayerSingletonPtr"sv;
		inline constexpr auto GetInputValue = "NCT:GetInputValue"sv;
		inline constexpr auto ShouldShowSneakCones = "NCT:ShouldShowSneakCones"sv;
		// ShouldShowSneakCones' first argument — the bucket table the Windows code
		// recovered by rel32-decoding inside UnkSingletonFn's body, which cannot be
		// done against clang output. Synthetic GLOBAL row, va 0x7c061e8 (tracker
		// 0x141b48c40-unk-singleton-ptr: Win 0x146291718 -> Linux 0x7c061e8,
		// verified-adversarial).
		inline constexpr auto UnkSingletonPtr = "NCT:UnkSingletonPtr"sv;

		// --- Hooks::Hook ---
		inline constexpr auto UpdateCamera = "E8 ?? ?? ?? ?? 48 8D 8D F8 04 00 00 E8 ?? ?? ?? ?? E9 AF FD FF FF"sv;
		inline constexpr auto HandleCameraInput = "E8 ?? ?? ?? ?? 0F B7 08 66 89 0B 80 3B 00"sv;
		inline constexpr auto CalculateCameraPitch = "E8 ?? ?? ?? ?? 80 BF 4C 01 00 00 00"sv;
		inline constexpr auto UpdateCameraPitch = "E8 ?? ?? ?? ?? 48 8B 46 70 4C 8D 45 90 0F 28 46 30"sv;
		inline constexpr auto AfterUpdateCameraZoom = "E8 ?? ?? ?? ?? 80 BF 54 02 00 00 00"sv;
		inline constexpr auto HandleToggleInputMode = "E8 ?? ?? ?? ?? 0F B7 00 84 C0"sv;
		inline constexpr auto SetDefaultZoom = "E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? E9 81 02 00 00"sv;
		inline constexpr auto SDLMouseY = "E8 ?? ?? ?? ?? 48 8B 8D F8 00 00 00 48 33 CC E8 ?? ?? ?? ?? 4C 8D 9C 24 40 02 00 00"sv;
	}  // namespace Keys

	// Thin wrappers over the catalog so the log lines stay uniform and every
	// unresolved key produces exactly one WARN with the key in it.
	namespace Catalog
	{
		using Site = dku::Hook::SiteCatalog::Site;
		using Kind = dku::Hook::SiteCatalog::Kind;
		using Relation = dku::Hook::SiteCatalog::Relation;

		[[nodiscard]] inline const Site* Lookup(std::string_view a_key) noexcept
		{
			return dku::Hook::SiteCatalog::get().lookup(a_key);
		}

		// Absolute runtime address, or 0. Only `relation: equivalent` resolves —
		// an inlined target's address is the HOST, and DKUtil refuses to hand that
		// to a call rewrite (SiteCatalog.hpp).
		[[nodiscard]] inline std::uintptr_t Resolve(std::string_view a_key) noexcept
		{
			return dku::Hook::SiteCatalog::get().resolve(a_key);
		}

		[[nodiscard]] inline std::uintptr_t ResolveHost(std::string_view a_key, std::uintptr_t& a_outGuest) noexcept
		{
			a_outGuest = 0;
			return dku::Hook::SiteCatalog::get().resolve_host(a_key, &a_outGuest);
		}

		// Two-window form, for a target whose Windows counterpart was a WRAP and whose
		// catalog row therefore declares a `guest_exit` alongside its `guest_entry`
		// (today: only UpdateCameraPitch). `a_outExit` stays 0 for every row that does
		// not declare one, which is every other row — the field is optional by design
		// (DKUtil Impl/Hook/SiteCatalog.hpp).
		[[nodiscard]] inline std::uintptr_t ResolveHost(std::string_view a_key,
			std::uintptr_t& a_outGuest, std::uintptr_t& a_outExit) noexcept
		{
			a_outGuest = 0;
			a_outExit = 0;
			return dku::Hook::SiteCatalog::get().resolve_host(a_key, &a_outGuest, &a_outExit);
		}

		[[nodiscard]] inline const char* KindName(Kind a_kind) noexcept
		{
			switch (a_kind) {
			case Kind::CallSite: return "CALL_SITE";
			case Kind::FuncEntry: return "FUNC_ENTRY";
			case Kind::Global: return "GLOBAL";
			case Kind::PatchSite: return "PATCH_SITE";
			case Kind::VtableSlot: return "VTABLE_SLOT";
			default: return "UNKNOWN";
			}
		}

		// A catalog row may legitimately carry `kind: unknown` (the generator infers
		// kind and leaves it blank when it cannot). Accept the expected kind or an
		// unstated one; refuse anything that is positively a different kind, because
		// that means the row was re-purposed and our use of the address is wrong.
		[[nodiscard]] inline bool KindOk(std::string_view a_key, Kind a_expected) noexcept
		{
			const auto* site = Lookup(a_key);
			if (!site) {
				return false;
			}
			if (site->kind == a_expected || site->kind == Kind::Unknown) {
				return true;
			}
			WARN("site '{}' is {} in the catalog but this mod uses it as {} — refusing it",
				a_key, KindName(site->kind), KindName(a_expected))
			return false;
		}

		// Cheap self-defence for the three write_call<5> sites: the catalog must hand
		// back the address OF THE E8, not the callee entry. A stale row that still
		// records the function entry would otherwise make write_call splice a rel32
		// over a function prologue.
		//
		// The dereference is only safe because it is bounds-checked first. Every
		// address here is an absolute .text vaddr valid for exactly one game build;
		// against any other program (a patched game, or the loader's fake-bg3 test
		// harness) it lands outside the mapped image and reading it is a SIGSEGV
		// inside dlopen. DKUtil's SiteCatalog::resolve() already refuses an
		// out-of-range row, so this is belt-and-braces — but it is the check that
		// makes the deref on the next line defensible on its own terms.
		[[nodiscard]] inline bool LooksLikeDirectCall(std::uintptr_t a_site) noexcept
		{
			if (a_site == 0) {
				return false;
			}
			if (!dku::Hook::SiteCatalog::module_text_contains(a_site)) {
				ERROR("catalog site {:X} is not inside the running program's executable segment — "
					  "refusing to read it (stale catalog / wrong game build)",
					a_site)
				return false;
			}
			return *reinterpret_cast<const std::uint8_t*>(a_site) == 0xE8;
		}

		// The same self-defence for the cave/patch sites, which are pinned in
		// LinuxLayout.h rather than fetched from the catalog: read the reviewed
		// wildcard-free byte run around the site and refuse to patch unless it matches
		// exactly. `a_at` is where the run starts relative to a_site (negative = before
		// it). Bounds-checked at BOTH ends before any dereference, for the same reason
		// LooksLikeDirectCall is.
		[[nodiscard]] inline bool BytesMatch(std::uintptr_t a_site, std::ptrdiff_t a_at,
			const std::uint8_t* a_expected, std::size_t a_size, const char* a_name) noexcept
		{
			if (a_site == 0 || a_expected == nullptr || a_size == 0) {
				return false;
			}
			const auto begin = static_cast<std::uintptr_t>(static_cast<std::ptrdiff_t>(a_site) + a_at);
			if (!dku::Hook::SiteCatalog::module_text_contains(begin) ||
				!dku::Hook::SiteCatalog::module_text_contains(begin + a_size - 1)) {
				ERROR("{}: byte window at {:X}..{:X} is outside the running program's executable "
					  "segment — refusing to read or patch it (stale catalog / wrong game build)",
					a_name, begin, begin + a_size - 1)
				return false;
			}
			const auto* actual = reinterpret_cast<const std::uint8_t*>(begin);
			for (std::size_t i = 0; i < a_size; ++i) {
				if (actual[i] != a_expected[i]) {
					ERROR("{}: byte {} at {:X} is {:02X}, expected {:02X} — this is not the reviewed "
						  "instruction window; refusing to patch (wrong game build?)",
						a_name, i, begin + i, actual[i], a_expected[i])
					return false;
				}
			}
			return true;
		}
	}  // namespace Catalog
#endif  // !_WIN32

	class Offsets
	{
	public:
		static bool Init()
		{
			bool bSuccess = true;

#if defined(_WIN32)
			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"48 8B 05 ?? ?? ?? ?? 80 B8 32 13 00 00 00 74 07">());
				if (scan) {
					auto singletonOffset = *reinterpret_cast<int32_t*>(scan + 3);
					UnkCameraSingletonPtr = reinterpret_cast<void**>(scan + 7 + singletonOffset);
					GetCurrentCameraDefinition = reinterpret_cast<tGetCurrentCameraDefinition>(scan);
					INFO("GetCurrentCameraDefinition found: {:X}", AsAddress(GetCurrentCameraDefinition) - dku::Hook::Module::get().base())
				} else {
					ERROR("GetCurrentCameraDefinition not found!")
					bSuccess = false;
				}
			}

			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"48 8B 0D ?? ?? ?? ?? 0F B7 D0 E8 ?? ?? ?? ?? 84 C0 75 1B">());
				if (scan) {
					auto unkSingletonOffset = *reinterpret_cast<int32_t*>(scan + 3);
					auto funcOffset = *reinterpret_cast<int32_t*>(scan + 0xB);
					UnkSingletonPtr = reinterpret_cast<void**>(scan + 7 + unkSingletonOffset);
					ShouldShowSneakCones = reinterpret_cast<tShouldShowSneakCones>(scan + 0xF + funcOffset);

					auto inputSingletonOffset = *reinterpret_cast<int32_t*>(AsAddress(ShouldShowSneakCones) + 0x82 + 3);
					UnkInputSingletonPtr = reinterpret_cast<void**>(AsAddress(ShouldShowSneakCones) + 0x82 + 7 + inputSingletonOffset);

					auto getInputValueCallsite = AsAddress(ShouldShowSneakCones) + 0x9B;
					auto getInputValueOffset = *reinterpret_cast<int32_t*>(getInputValueCallsite + 1);
					GetInputValue = reinterpret_cast<tGetInputValue>(getInputValueCallsite + 5 + getInputValueOffset);

					INFO("Input related functions found: {:X}", AsAddress(ShouldShowSneakCones) - dku::Hook::Module::get().base())
				} else {
					ERROR("Input related functions not found!")
					bSuccess = false;
				}
			}

			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"48 8B 0D ?? ?? ?? ?? 0F B7 D7 E8 ?? ?? ?? ?? 3C FF">());
				if (scan) {
					auto playerSingletonOffset = *reinterpret_cast<int32_t*>(scan + 3);
					UnkPlayerSingletonPtr = reinterpret_cast<void**>(scan + 7 + playerSingletonOffset);

					auto getPlayerControllerCallsite = scan + 0xA;
					auto getPlayerControllerOffset = *reinterpret_cast<int32_t*>(getPlayerControllerCallsite + 1);
					GetPlayerController = reinterpret_cast<tGetPlayerController>(getPlayerControllerCallsite + 5 + getPlayerControllerOffset);

					INFO("Player controller related functions found: {:X}", AsAddress(GetPlayerController) - dku::Hook::Module::get().base())
				} else {
					ERROR("Player controller functions not found!")
					bSuccess = false;
				}
			}

			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"80 3D ?? ?? ?? ?? ?? 74 22">());
				if (scan) {
					auto offset = *reinterpret_cast<int32_t*>(scan + 2);
					bIsInControllerMode = reinterpret_cast<bool*>(scan + 7 + offset);
					INFO("bIsInControllerMode found: {:X}", AsAddress(bIsInControllerMode) - dku::Hook::Module::get().base())
				} else {
					ERROR("bIsInControllerMode not found!")
					bSuccess = false;
				}
			}

			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 41 0F 28 45 40">());
				if (scan) {
					auto offset = *reinterpret_cast<int32_t*>(scan + 1);
					GetCharacter = reinterpret_cast<tGetCharacter>(scan + 5 + offset);
					INFO("GetCharacter found: {:X}", AsAddress(GetCharacter) - dku::Hook::Module::get().base())
				} else {
					ERROR("GetCharacter not found!")
					bSuccess = false;
				}
			}

			// unused
			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"48 89 5C 24 08 57 48 83 EC 30 48 8B 41 10 48 8B D9">());
				if (scan) {
					GetCharacterHeight = reinterpret_cast<tGetCharacterHeight>(scan);
					INFO("GetCharacterHeight found: {:X}", AsAddress(GetCharacterHeight) - dku::Hook::Module::get().base())
				} else {
					ERROR("GetCharacterHeight not found!")
					bSuccess = false;
				}
			}

			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? EB 11 33 C9">());
				if (scan) {
					auto offset = *reinterpret_cast<int32_t*>(scan + 1);
					GetFloorLevel = reinterpret_cast<tGetFloorLevel>(scan + 5 + offset);
					INFO("GetFloorLevel found: {:X}", AsAddress(GetFloorLevel) - dku::Hook::Module::get().base())
				} else {
					ERROR("GetFloorLevel not found!")
					bSuccess = false;
				}
			}

			{
				auto scan = static_cast<uint8_t*>(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 0F 28 DA F3 0F 5C E5">());
				if (scan) {
					auto offset = *reinterpret_cast<int32_t*>(scan + 1);
					GetCameraMinZoom = reinterpret_cast<tGetCameraMinZoom>(scan + 5 + offset);
					INFO("GetCameraMinZoom found: {:X}", AsAddress(GetCameraMinZoom) - dku::Hook::Module::get().base())
				} else {
					ERROR("GetCameraMinZoom not found!")
					bSuccess = false;
				}
			}
#else
			// ---------------------------------------------------------------
			// Linux. Nothing below byte-scans and nothing rel32-decodes: every
			// address comes out of the site catalog already resolved, and any key
			// that is still unresolved yields 0 -> the feature that needs it turns
			// itself off (never a crash, never a guessed address).
			// ---------------------------------------------------------------
			const auto moduleBase = dku::Hook::Module::get().base();
			const auto rva = [moduleBase](std::uintptr_t a_abs) { return a_abs - moduleBase; };

			// Resolve + kind-check in one step. Resolve() is always called first so
			// that an absent/unresolved key produces its own one-shot WARN naming the
			// key, rather than only showing up in an aggregate message later.
			const auto resolveTyped = [](std::string_view a_key, Catalog::Kind a_expected) -> std::uintptr_t {
				const auto va = Catalog::Resolve(a_key);
				if (!va) {
					return 0;
				}
				return Catalog::KindOk(a_key, a_expected) ? va : 0;
			};

			// GetCurrentCameraDefinition: INLINED at every Linux call site
			// (tracker 0x141c714e0-get-current-camera-definition, review
			// 2026-09-01 CONFIRMED — 19 hosts, no standalone counterpart exists).
			// There is nothing callable, so the pointer stays null forever and the
			// mod's own Utils::GetCurrentCameraDefinition reimplementation is used.
			// The key itself is a GLOBAL on Linux: ls::GlobalSwitches.
			GetCurrentCameraDefinition = nullptr;
			{
				const auto va = resolveTyped(Keys::GlobalSwitches, Catalog::Kind::Global);
				if (va) {
					// Same shape as Windows: the catalog hands back the address of the
					// POINTER slot, so `*UnkCameraSingletonPtr` is the object.
					UnkCameraSingletonPtr = reinterpret_cast<void**>(va);
					INFO("UnkCameraSingletonPtr (ls::GlobalSwitches) resolved: {:X}", rva(va))
					INFO("GetCurrentCameraDefinition is inlined on Linux — using the mod's own reimplementation (offsets bool {:X} / unk {:X} / exploration {:X} / combat {:X})",
						cameraBoolOffset, unkCameraOffset, explorationCameraOffset, combatCameraOffset)
				} else {
					ERROR("UnkCameraSingletonPtr (ls::GlobalSwitches) unresolved — camera settings, pitch unlock and zoom limits are all off")
					bSuccess = false;
				}
			}

			// The mod used to byte-walk INTO this function's body to recover
			// UnkInputSingletonPtr and GetInputValue. That is impossible against
			// clang output, and the catalog now resolves this key to the FUNC_ENTRY
			// of the containing function only (tracker 0x141b48c40-unk-singleton-ptr,
			// review 2026-09-01). Log it and derive NOTHING from it.
			{
				const auto fn = resolveTyped(Keys::UnkSingletonFn, Catalog::Kind::FuncEntry);
				if (fn) {
					INFO("UnkSingletonPtr containing fn resolved: {:X} (no values are derived from its body on Linux)", rva(fn))
				}
			}

			// ShouldShowSneakCones: there is NO standalone Linux counterpart. Tracker
			// 0x14182acc0-should-show-sneak-cones (verified-adversarial, reviewer opus-5
			// nct-re-remainder 2026-09-03) proves its absence exhaustively — the body
			// exists only as two inlined copies (host 0x2d1dad0 @0x2d1daf0 and host
			// 0x2d21e60 @0x2d2283a). Its catalog row is `relation: inlined_into`, so
			// Catalog::Resolve() returns 0 for it BY DESIGN. The function pointer stays
			// null forever and Offsets::ShouldShowSneakConesLinux (Hooks.cpp) is the
			// only path — same split as GetCurrentCameraDefinition and
			// GetPlayerController.
			ShouldShowSneakCones = nullptr;
			{
				std::uintptr_t guest = 0;
				const auto     host = Catalog::ResolveHost(Keys::ShouldShowSneakCones, guest);
				if (host) {
					INFO("ShouldShowSneakCones is inlined on Linux (host {:X}, body {:X}) — using the mod's own reimplementation",
						rva(host), guest ? rva(guest) : 0)
				}
			}

			// GetInputValue: the Linux function is NOT the Windows one's twin —
			// arity 4 -> 3, the id is passed by VALUE in esi, the player index is a
			// byte in dl (0xFF = absent) and the result comes back in registers
			// instead of through an out-param. Storing it in `tGetInputValue` (the
			// MSVC prototype) and calling it would put a stack address where the
			// callee expects that byte index. The Windows-shaped pointer therefore
			// stays null on Linux and only the correctly-typed one is ever called
			// — same split as GetFloorLevel / GetFloorLevelLinux.
			GetInputValue = nullptr;
			GetInputValueLinux = reinterpret_cast<tGetInputValueLinux>(resolveTyped(Keys::GetInputValue, Catalog::Kind::FuncEntry));

			UnkInputSingletonPtr = reinterpret_cast<void**>(resolveTyped(Keys::gInputManager, Catalog::Kind::Global));
			UnkPlayerSingletonPtr = reinterpret_cast<void**>(resolveTyped(Keys::UnkPlayerSingletonPtr, Catalog::Kind::Global));

			// ShouldShowSneakCones' first argument (the first bucket table). The
			// Windows code recovered it by rel32-decoding inside UnkSingletonFn's body,
			// which is impossible against clang output — but the pipeline has since
			// minted a synthetic GLOBAL row for it, so it is resolved the same way as
			// every other address in this file rather than hardcoded here.
			// tracker 0x141b48c40-unk-singleton-ptr: Win 0x146291718 -> Linux 0x7c061e8.
			UnkSingletonPtr = reinterpret_cast<void**>(resolveTyped(Keys::UnkSingletonPtr, Catalog::Kind::Global));
			if (UnkSingletonPtr) {
				INFO("UnkSingletonPtr (ShouldShowSneakCones bucket table) resolved: {:X}", rva(AsAddress(UnkSingletonPtr)))
			}

			if (ShouldShowSneakCones) {
				INFO("ShouldShowSneakCones resolved: {:X}", rva(AsAddress(ShouldShowSneakCones)))
			}
			if (GetInputValueLinux) {
				INFO("GetInputValue resolved: {:X} (Linux prototype: 3 args — manager, id BY VALUE, player-index byte; returns RE::InputValue in xmm0:rax)",
					rva(AsAddress(GetInputValueLinux)))
			}
			if (UnkInputSingletonPtr) {
				INFO("UnkInputSingletonPtr (ls::gInputManager) resolved: {:X}", rva(AsAddress(UnkInputSingletonPtr)))
			}
			if (UnkPlayerSingletonPtr) {
				INFO("UnkPlayerSingletonPtr resolved: {:X}", rva(AsAddress(UnkPlayerSingletonPtr)))
			}

			// The player-controller lookup itself: clang inlined the Windows callee
			// (0x143baaeb0) into 0x25c6cd0, so there is no function pointer to take.
			// LookupPlayerController below reimplements the bucket walk.
			GetPlayerController = nullptr;
			{
				const auto fn = Catalog::Resolve(Keys::PlayerControllerFn);
				if (fn) {
					INFO("Player-controller containing fn resolved: {:X}; GetPlayerController is inlined there — using the reimplemented bucket lookup", rva(fn))
				}
			}

			// The controller zoom-vs-pitch branch needs exactly four addresses, all
			// four now catalog rows (the fifth, ShouldShowSneakCones itself, is
			// reimplemented in-mod and so is deliberately NOT part of this gate):
			//   NCT:UnkSingletonPtr        — the first bucket table (presence test)
			//   NCT:UnkPlayerSingletonPtr  — the second bucket table (sub-id, 0xFF absent)
			//   NCT:gInputManager          — GetInputValue's `this`
			//   NCT:GetInputValue          — the callee itself
			bControllerZoomAvailable =
				UnkPlayerSingletonPtr != nullptr &&
				UnkSingletonPtr != nullptr &&
				GetInputValueLinux != nullptr &&
				UnkInputSingletonPtr != nullptr;
			if (!bControllerZoomAvailable) {
				WARN("controller zoom/pitch swap disabled on Linux: unresolved {}{}{}{}",
					UnkPlayerSingletonPtr ? "" : "UnkPlayerSingletonPtr ",
					UnkSingletonPtr ? "" : "UnkSingletonPtr ",
					GetInputValueLinux ? "" : "GetInputValue ",
					UnkInputSingletonPtr ? "" : "UnkInputSingletonPtr ")
			} else {
				INFO("controller zoom/pitch swap available on Linux (ShouldShowSneakCones reimplemented in-mod)")
			}

			{
				const auto va = resolveTyped(Keys::IsInControllerMode, Catalog::Kind::Global);
				if (va) {
					// GLOBAL: the catalog already did the rip-decode. The Windows path
					// does scan+7+disp32, which would be nonsense here.
					bIsInControllerMode = reinterpret_cast<bool*>(va);
					INFO("bIsInControllerMode resolved: {:X}", rva(va))
				} else {
					WARN("bIsInControllerMode unresolved — controller-specific camera behaviour is off")
				}
			}

			// GetFloorLevel: FUNC_ENTRY, taken as-is. NCT calls it, never hooks it,
			// and the Linux prototype differs (see tGetFloorLevelLinux).
			{
				const auto va = resolveTyped(Keys::GetFloorLevel, Catalog::Kind::FuncEntry);
				if (va) {
					GetFloorLevelLinux = reinterpret_cast<tGetFloorLevelLinux>(va);
					INFO("GetFloorLevel resolved: {:X} (Linux prototype: no out-param, no dead a5, {{float,bool}} returned in rax:rdx)", rva(va))
				} else {
					WARN("GetFloorLevel unresolved — under-floor zoom clipping is off")
				}
			}

			// Dead on Linux. GetCharacter/GetCharacterHeight/GetCameraMinZoom have no
			// live call site in this mod (GetCameraMinZoom's is commented out at
			// CameraTweaksManager.cpp:280; the other two are never called at all), and
			// the tracker closed 0x141feb850 as `obviated`. Do not resolve them.
			GetCharacter = nullptr;
			GetCharacterHeight = nullptr;
			GetCameraMinZoom = nullptr;
			INFO("skipping GetCharacter / GetCharacterHeight / GetCameraMinZoom on Linux: dead code in this mod (tracker 0x141feb850-nct-camera-fn-a: obviated)")
#endif

			return bSuccess;
		}

		enum class InputID : int32_t
		{
		    kZoomIn = 104,
			kZoomOut = 105,
			kRotateLeft = 107,
			kRotateRight = 108,
			kMouseRotateLeft = 109,
			kMouseRotateRight = 110,
			kToggleInputMode = 0xC0
		};

#if defined(_WIN32)
		constexpr static inline uint32_t cameraBoolOffset = 0x1332;
		constexpr static inline uint32_t unkCameraOffset = 0xC58;
		constexpr static inline uint32_t explorationCameraOffset = 0x79C;
		constexpr static inline uint32_t combatCameraOffset = 0x930;
#else
		// Linux values live in LinuxLayout.h with their tracker key and review date.
		constexpr static inline uint32_t cameraBoolOffset = NCT::LinuxLayout::kGlobalSwitches_cameraBoolOffset;
		constexpr static inline uint32_t unkCameraOffset = NCT::LinuxLayout::kGlobalSwitches_unkCameraOffset;
		constexpr static inline uint32_t explorationCameraOffset = NCT::LinuxLayout::kGlobalSwitches_explorationCameraOffset;
		constexpr static inline uint32_t combatCameraOffset = NCT::LinuxLayout::kGlobalSwitches_combatCameraOffset;
#endif

		using tGetCurrentCameraDefinition = RE::CameraDefinition* (*)(RE::CameraObject* a1);
		using tShouldShowSneakCones = bool (*)(void* a1, int16_t a_playerId);
		using tGetCharacter = uintptr_t (*)(uintptr_t a1, int16_t a_playerId);
		using tGetCharacterHeight = float (*)(uintptr_t a_character);
		using tGetPlayerController = void* (*)(void* a1, int16_t a_playerId);
		using tGetInputValue = RE::InputValue* (*)(void* a1, RE::InputValue& a_outValue, InputID& a_inputId, void* a4);
		using tGetCurrentPlayerInternal = RE::Player* (*)(uint64_t a1, uint64_t a2);
		using tGetFloorLevel = RE::FloorLevelStruct* (*)(RE::FloorLevelStruct& a_outFloorLevelStruct, uint64_t a2, bool a3, RE::CameraDefinition* a_cameraDefinition, void* a5, RE::Vector3& a_cameraPos, uint64_t a7);
		using tGetCameraMinZoom = float (*)(RE::CameraModeFlags a_flags, bool a2);

#if !defined(_WIN32)
		// Linux GetFloorLevel (0x2694850).
		//
		// tracker 0x141febcb0-get-floor-level, review 2026-09-01 CONFIRMED:
		// "MSVC hidden sret vanishes under Itanium" — the `RE::FloorLevelStruct&`
		// out-param AND the redundant pointer return both collapse into a
		// {float,bool} pair returned in rax:rdx, and the always-null `void* a5` is
		// dead-arg-eliminated. Handing this the Windows 7-arg list would put
		// &floorLevelStruct in rdi where the callee expects its first real argument.
		//
		// RE::FloorLevelStruct is byte-compatible with what the callee returns
		// (float@0, bool@4, bool@8, size 12), so SysV classifies it INTEGER/INTEGER
		// and it comes back in exactly the rax:rdx pair the disassembly shows.
		using tGetFloorLevelLinux = RE::FloorLevelStruct (*)(uint64_t a2, bool a3, RE::CameraDefinition* a_cameraDefinition, RE::Vector3& a_cameraPos, uint64_t a7);
		static_assert(sizeof(RE::FloorLevelStruct) == 12, "GetFloorLevel's Linux return must stay a 12-byte INTEGER/INTEGER pair (rax:rdx)");
		static_assert(offsetof(RE::FloorLevelStruct, unk08) == 8, "unk08 must sit in the second eightbyte (rdx)");

		// Linux GetInputValue (0x2d1d880).
		//
		// tracker 0x144125ce0-get-input-value-a (cataloged/verified-adversarial,
		// reviewer opus-5 nct-re-remainder 2026-09-03). The catalog row for
		// `NCT:GetInputValue` states it outright: "Linux prototype differs from the
		// Windows tGetInputValue (arity 4->3, id by-value not pointer)".
		//
		// Read off `bg3re disasm 0x2d1d880`:
		//   rdi = the input manager (`mov r8d,[rdi+rcx*1+0x154]` @0x2d1d894)
		//   esi = the input id BY VALUE (`mov eax,esi` @0x2d1d8ad, `div r8d`,
		//         `cmp [rdx+0x8],esi` @0x2d1d8cc)
		//   dl  = the player-index byte, 0xFF = absent (`cmp dl,0xff` @0x2d1d883
		//         -> the early-out at 0x2d1d989, `movsx rcx,dl` @0x2d1d88c)
		// and the game's own caller agrees: `mov rdi,[rip]#7d9d0a8 (gInputManager)`,
		// `mov esi,0xdc`, `movsx edx,r8b`, `call 2d1d880`, `test al,al`
		// (0x2d1db58-0x2d1db6d) — three arguments, id as an immediate.
		//
		// Return is BY VALUE, not through an out-param: `movsd xmm0,[rax+rdx*4]`
		// @0x2d1d949 loads the two floats as one 8-byte SSE eightbyte, the flag byte
		// is accumulated in ecx and returned via `mov eax,ecx; ret` @0x2d1d981.
		// RE::InputValue is {float,float,bool} = 12 bytes, which SysV classifies
		// SSE(xmm0) + INTEGER(rax) — exactly that pair. The static_asserts below
		// pin the classification so a field added to RE::InputValue becomes a
		// compile error rather than a silently mis-read return.
		using tGetInputValueLinux = RE::InputValue (*)(void* a_manager, int32_t a_inputId, uint8_t a_playerController);
		static_assert(sizeof(RE::InputValue) == 12, "GetInputValue's Linux return must stay 12 bytes (SSE eightbyte + INTEGER eightbyte)");
		static_assert(offsetof(RE::InputValue, valueA) == 0 && offsetof(RE::InputValue, valueB) == 4,
			"the two floats must share the first eightbyte so it classifies SSE (xmm0)");
		static_assert(offsetof(RE::InputValue, bIsPressed) == 8, "the flag must sit in the second eightbyte (rax)");

		// Reimplementation of the Windows GetPlayerController (0x143baaeb0), which
		// clang inlined into 0x25c6cd0 (tracker 0x142067ba0-player-controller-fns,
		// review 2026-09-01; loop re-derived 2026-09-03). a_map is the object the
		// singleton slot points at, i.e. `*UnkPlayerSingletonPtr`.
		// Returns LinuxLayout::kPlayerControllerAbsent (0xFF) when absent — the same
		// sentinel both builds use.
		[[nodiscard]] static uint8_t LookupPlayerController(void* a_map, int16_t a_playerId) noexcept;

		// Reimplementation of the Windows ShouldShowSneakCones (0x14182acc0), which
		// has NO standalone Linux counterpart at all (tracker
		// 0x14182acc0-should-show-sneak-cones, reviewer opus-5 CONFIRMED 2026-09-03:
		// exhaustive absence proof, two inlined copies only). Same two-table walk,
		// same action id, but through the Linux GetInputValue prototype.
		// a_table is the object the first singleton slot points at, i.e.
		// `*UnkSingletonPtr` — exactly what the Windows call site passes in rcx.
		[[nodiscard]] static bool ShouldShowSneakConesLinux(void* a_table, int16_t a_playerId) noexcept;
#endif

		static inline tGetCurrentCameraDefinition GetCurrentCameraDefinition;
		static inline tShouldShowSneakCones ShouldShowSneakCones;
		static inline tGetCharacter GetCharacter;
		static inline tGetCharacterHeight GetCharacterHeight;
		static inline tGetPlayerController GetPlayerController;
		static inline tGetInputValue GetInputValue;
		static inline tGetCurrentPlayerInternal GetCurrentPlayerInternal;
		static inline tGetCameraMinZoom GetCameraMinZoom;
		static inline tGetFloorLevel GetFloorLevel;
#if !defined(_WIN32)
		static inline tGetFloorLevelLinux GetFloorLevelLinux = nullptr;
		static inline tGetInputValueLinux GetInputValueLinux = nullptr;
		// Set by Init(): every input the controller zoom/pitch-swap branch needs is
		// resolved. False disables that branch (Hook_HandleCameraInput).
		static inline bool bControllerZoomAvailable = false;
#endif

		static inline void** UnkSingletonPtr = nullptr;
		static inline void** UnkCameraSingletonPtr = nullptr;
		static inline void** UnkPlayerSingletonPtr = nullptr;
		static inline void** UnkInputSingletonPtr = nullptr;
		static inline bool* bIsInControllerMode = nullptr;
	};

    class Hooks
    {
    public:
		static bool Hook()
		{
			bool bSuccess = true;

#if defined(_WIN32)
			dku::Hook::Trampoline::AllocTrampoline(1 << 7);
#else
			// Linux needs more than upstream's 128 bytes, and the failure mode is
			// not a skipped hook — `Trampoline::do_allocate` FATALs when a write
			// exceeds capacity (Trampoline.hpp), and on Linux DKUtil's
			// report_error(true, ...) ends in std::abort(), i.e. the game dies
			// inside dlopen.
			//
			// Measured budget (Hooks.cpp's own CaveProlog/CaveEpilog/ToggleGate
			// assembled with the vendored Xbyak and read back with getSize()):
			// prolog 140-150 B, epilog 133-146 B, plus what AddCaveHook itself emits
			// per cave — 8 (movabs callback) + 4 (sub rsp) + 6 (call [rip]) + 4 (add
			// rsp) + up to 7 stolen bytes replayed under kRestoreAfterEpilog + 5 (jmp
			// rel32) — so ~320 B for ONE cave, ~1.3 KB for the four this mod installs.
			// The toggle gate is 61 B and each of the three live write_call rel hooks
			// ~14 B. 128 B could not fit a single prolog; one page fits everything
			// with room to spare, and an over-reservation costs one mmap'd page.
			dku::Hook::Trampoline::AllocTrampoline(NCT::LinuxLayout::kTrampolineSize);
#endif

#if defined(_WIN32)
			const auto UpdateCameraCallAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 48 8D 8D F8 04 00 00 E8 ?? ?? ?? ?? E9 AF FD FF FF">());
			if (UpdateCameraCallAddress) {
				_UpdateCamera = dku::Hook::write_call<5>(UpdateCameraCallAddress, Hook_UpdateCamera);
				INFO("Hooked UpdateCamera: {:X}", AsAddress(UpdateCameraCallAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("UpdateCamera not found!")
				bSuccess = false;
			}

			const auto HandleCameraInputAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 0F B7 08 66 89 0B 80 3B 00">());
			if (HandleCameraInputAddress) {
				_HandleCameraInput = dku::Hook::write_call<5>(HandleCameraInputAddress, Hook_HandleCameraInput);
				INFO("Hooked HandleCameraInput: {:X}", AsAddress(HandleCameraInputAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("HandleCameraInput not found!")
			    bSuccess = false;
			}

			const auto CalculateCameraPitchAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 80 BF 4C 01 00 00 00">());
			if (CalculateCameraPitchAddress) {
				_CalculateCameraPitch = dku::Hook::write_call<5>(CalculateCameraPitchAddress, Hook_CalculateCameraPitch);
				INFO("Hooked CalculateCameraPitch: {:X}", AsAddress(CalculateCameraPitchAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("CalculateCameraPitch not found!")
				bSuccess = false;
			}

			const auto UpdateCameraPitchAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 48 8B 46 70 4C 8D 45 90 0F 28 46 30">());
			if (UpdateCameraPitchAddress) {
				_UpdateCameraPitch = dku::Hook::write_call<5>(UpdateCameraPitchAddress, Hook_UpdateCameraPitch);
				INFO("Hooked UpdateCameraPitch: {:X}", AsAddress(UpdateCameraPitchAddress) - dku::Hook::Module::get().base())
            } else {
				ERROR("UpdateCameraPitch not found!")
				bSuccess = false;
            }

			const auto AfterUpdateCameraZoomAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 80 BF 54 02 00 00 00">());
			if (AfterUpdateCameraZoomAddress) {
				_AfterUpdateCameraZoom = dku::Hook::write_call<5>(AfterUpdateCameraZoomAddress, Hook_AfterUpdateCameraZoom);
				INFO("Hooked AfterUpdateCameraZoom: {:X}", AsAddress(AfterUpdateCameraZoomAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("AfterUpdateCameraZoom not found!")
				bSuccess = false;
			}

			const auto HandleToggleInputModeCallAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 0F B7 00 84 C0">());
			if (HandleToggleInputModeCallAddress) {
				_HandleToggleInputMode = dku::Hook::write_call<5>(HandleToggleInputModeCallAddress, Hook_HandleToggleInputMode);
				INFO("Hooked HandleToggleInputModeCall: {:X}", AsAddress(HandleToggleInputModeCallAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("HandleToggleInputModeCall not found!")
				bSuccess = false;
			}

			const auto SetDefaultZoomCallAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? E9 81 02 00 00">());
			if (SetDefaultZoomCallAddress) {
				_SetDefaultZoom = dku::Hook::write_call<5>(SetDefaultZoomCallAddress, Hook_SetDefaultZoom);
				INFO("Hooked SetDefaultZoom: {:X}", AsAddress(SetDefaultZoomCallAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("SetDefaultZoom not found!")
				bSuccess = false;
			}

			const auto SDLMouseYHookAddress = AsAddress(dku::Hook::Assembly::search_pattern<"E8 ?? ?? ?? ?? 48 8B 8D F8 00 00 00 48 33 CC E8 ?? ?? ?? ?? 4C 8D 9C 24 40 02 00 00">());
			if (SDLMouseYHookAddress) {
				struct Stub : Xbyak::CodeGenerator
				{
					Stub()
					{
						mov(r9d, r12d);
						mov(rax, (uintptr_t)&Hook_SDLMouseYHook);
						jmp(rax);
					}
				};
			    static Stub stub;

				_SDLMouseYHook = dku::Hook::write_call<5>(SDLMouseYHookAddress, (bool (*)(uint64_t, uint64_t, bool, int))stub.getCode());

				INFO("Hooked SDLMouseYHook: {:X}", AsAddress(SDLMouseYHookAddress) - dku::Hook::Module::get().base())
			} else {
				ERROR("SDLMouseYHook not found!")
				bSuccess = false;
			}
#else
			bSuccess = HookLinux();
#endif

			return bSuccess;
		}

    private:
#if !defined(_WIN32)
		// Everything the Linux hook installation needs. Defined in Hooks.cpp.
		static bool HookLinux();

		// write_call<5> on a catalog CALL_SITE, with the E8 sanity check.
		// Returns the original callee, or a null pointer (and logs) if the site is
		// unusable — every caller must treat null as "hook not installed".
		template <typename F>
		static F WriteCallSite(std::string_view a_key, const char* a_name, F a_hook)
		{
			const auto site = Catalog::Resolve(a_key);
			if (!site) {
				WARN("{} not hooked: site unresolved in the catalog", a_name)
				return F{};
			}
			if (!Catalog::KindOk(a_key, Catalog::Kind::CallSite)) {
				return F{};
			}
			if (!Catalog::LooksLikeDirectCall(site)) {
				ERROR("{} not hooked: catalog site {:X} does not start with E8 — the row is stale (it must record the CALL instruction, not the callee entry)",
					a_name, site - dku::Hook::Module::get().base())
				return F{};
			}
			F original = dku::Hook::write_call<5>(site, a_hook);
			INFO("Hooked {}: call site {:X} -> original {:X}", a_name,
				site - dku::Hook::Module::get().base(),
				AsAddress(original) - dku::Hook::Module::get().base())
			return original;
		}
#endif

		static void Hook_UpdateCamera(uint64_t a1, uint64_t a2, uint64_t a3, RE::UnkObject* a4);
		static void* Hook_HandleCameraInput(uint64_t a1, uint64_t a2, RE::UnkObject* a3, uintptr_t a4);
#if defined(_WIN32)
		static float Hook_CalculateCameraPitch(RE::CameraObject* a_cameraObject, uint8_t a2, uint8_t a3);
#else
		// tracker 0x141c719e0-calculate-camera-pitch, review 2026-09-01 CONFIRMED:
		// "3 params -> 2 (third param dead-arg-eliminated by LLVM)".
		static float Hook_CalculateCameraPitch(RE::CameraObject* a_cameraObject, uint8_t a2);
#endif
		static void Hook_UpdateCameraPitch(uint64_t a1, uint64_t a2, RE::CameraObject* a_cameraObject, uint64_t a4);
		static void Hook_AfterUpdateCameraZoom(uint64_t a1, uint64_t a2, RE::UnkObject* a3, uint64_t a4);
		static int16_t* Hook_HandleToggleInputMode(uint64_t a1, int16_t& a_outResult, Offsets::InputID* a_inputId);
		static void Hook_SetDefaultZoom(RE::CameraObject* a_cameraObject);
		static bool Hook_SDLMouseYHook(uint64_t a1, uint64_t a2, bool a3, int a_deltaY);

		static inline std::add_pointer_t<decltype(Hook_UpdateCamera)> _UpdateCamera;
		static inline std::add_pointer_t<decltype(Hook_HandleCameraInput)> _HandleCameraInput;
		static inline std::add_pointer_t<decltype(Hook_CalculateCameraPitch)> _CalculateCameraPitch;
		static inline std::add_pointer_t<decltype(Hook_UpdateCameraPitch)> _UpdateCameraPitch;
		static inline std::add_pointer_t<decltype(Hook_AfterUpdateCameraZoom)> _AfterUpdateCameraZoom;
		static inline std::add_pointer_t<decltype(Hook_HandleToggleInputMode)> _HandleToggleInputMode;
		static inline std::add_pointer_t<decltype(Hook_SetDefaultZoom)> _SetDefaultZoom;
		static inline std::add_pointer_t<decltype(Hook_SDLMouseYHook)> _SDLMouseYHook;
    };

	void Install();
}
