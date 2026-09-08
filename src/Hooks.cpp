#include "Hooks.h"

#include "CameraTweaksManager.h"
#include "Settings.h"
#include "Utils.h"

#if !defined(_WIN32)
#	include "Linux/LinuxLayout.h"
#endif

namespace Hooks
{
#if !defined(_WIN32)
	namespace
	{
		using namespace NCT::LinuxLayout;

		// One WARN per condition, ever.
		void WarnOnce(std::once_flag& a_flag, std::string a_message)
		{
			std::call_once(a_flag, [msg = std::move(a_message)] { WARN("{}", msg) });
		}

		// =================================================================
		// Cave-hook prolog/epilog.
		//
		// DKUtil's trampoline layout is: [prolog][sub rsp,0x20][call cb]
		// [add rsp,0x20][epilog][stolen bytes?][jmp back]. The callback is a plain
		// SysV C function, so it preserves rbx/rbp/r12-r15 by itself; everything
		// caller-saved is ours to save, and we must hand it a 16-byte-aligned rsp.
		//
		// %rbp is deliberately NOT touched and NOT used as a frame pointer here:
		// 0x141e0cab0's adversarial review (2026-09-01) found that in host
		// 0x2562df0 %rbp carries the high byte of the return value to the epilogue,
		// so a conventional frame setup would silently corrupt the result. The same
		// review lists rbx/rbp/r12/r13/r14/r15 as ALL live across the guest region;
		// the SysV callee-saved contract covers every one of them. In host
		// 0x2c7d3a0 %rbp is the live RE::CameraObject* for the same reason
		// (docs/linux-port/CAVE-HOOKS.md §7).
		//
		// Alignment bookkeeping (ASM_STACK_ALLOC_SIZE is 0x20, a multiple of 16, so
		// DKUtil's own sub/add does not disturb it):
		//   push rax          -> saves rax before we can use it as scratch
		//   pushfq            -> flags. ORDER IS LOAD-BEARING: this MUST precede the
		//                        `and rsp,-16` below, because AND writes SF/ZF/PF and
		//                        clears OF/CF. Saving after it would preserve the
		//                        AND's own result and hand THAT back to the host at
		//                        the resume point. The guest regions branch on flags,
		//                        so that is real corruption, not a nicety. (No live
		//                        site depends on it today -- every one of the four
		//                        resume addresses redefines flags before its first
		//                        reader -- but the mechanism is generic and its
		//                        contract must hold for the next cave, not just
		//                        these.)
		//   mov rax, rsp      -> remember the unaligned rsp; rax now points AT the
		//                        saved FLAGS, i.e. rax == hostRsp - 16. Every host
		//                        stack slot the callback needs is read through this
		//                        register, so no caller has to hand-adjust an rsp
		//                        displacement for the trampoline's own pushes.
		//   and rsp, -16      -> rsp % 16 == 0  (clobbers flags -- already saved)
		//   sub rsp, 8        -> % 16 == 8  parity slot, so the 9 pushes below still
		//                        leave the `call` 16-aligned
		//   push rax          -> % 16 == 0   [rsp] = old rsp
		//   8 x push (r/8)    -> % 16 == 0
		//   sub rsp, 0x100    -> % 16 == 0, room for xmm0-15
		// leaving rsp 16-aligned at the trampoline's `call`, exactly as SysV wants.
		// The epilog unwinds in mirror order: `pop rsp` (straight back to the
		// saved-flags slot, discarding the parity gap), `popfq`, `pop rax`.
		//
		// The 128-byte red zone below rsp is not preserved. All three cave hosts are
		// non-leaf functions with an explicit `sub rsp, imm` frame, so none uses it;
		// a future cave in a leaf function would need `sub rsp,0x80` first.
		// =================================================================

		// Where one callback argument comes from at the cave point.
		struct CaveArg
		{
			enum class From
			{
				kNone,
				kRegister,  // a live register at the cave
				kStackSlot  // a displacement off the HOST rsp at the cave
			};

			From         from{ From::kNone };
			int          reg{ -1 };   // Xbyak::Operand register code
			std::int32_t slot{ 0 };   // displacement off the HOST rsp

			static constexpr CaveArg None() noexcept { return {}; }
			static constexpr CaveArg Reg(int a_reg) noexcept { return { From::kRegister, a_reg, 0 }; }
			static constexpr CaveArg Slot(std::int32_t a_slot) noexcept { return { From::kStackSlot, -1, a_slot }; }
		};

		struct CaveProlog : Xbyak::CodeGenerator
		{
			CaveProlog(const CaveArg& a_arg1, const CaveArg& a_arg2, const CaveArg& a_arg3 = CaveArg::None())
			{
				using namespace Xbyak::util;
				push(rax);
				pushfq();       // BEFORE `and rsp,-16`: that AND defines SF/ZF/PF/OF/CF
				mov(rax, rsp);  // rax == hostRsp - 16, for the whole prolog
				and_(rsp, -16);
				sub(rsp, 8);    // parity slot; 9 pushes follow, keeping the `call` aligned
				push(rax);
				push(rcx);
				push(rdx);
				push(rsi);
				push(rdi);
				push(r8);
				push(r9);
				push(r10);
				push(r11);
				sub(rsp, 0x100);
				for (int i = 0; i < 16; ++i) {
					movups(ptr[rsp + i * 16], Xbyak::Xmm(i));
				}
				// Arguments last: rdi/rsi/rdx have already been saved above, and nothing
				// between here and DKUtil's `call` touches rax.
				//
				// arg3 (rdx) is materialised FIRST, before rdi/rsi are touched. Every
				// site that uses three arguments today (AfterUpdateCameraZoom) sources
				// arg3 from a HOST STACK SLOT read through rax, never from rdi/rsi
				// themselves, so writing rdx first cannot clobber anything arg1/arg2
				// still need to read, and arg1/arg2's own rdi/rsi writes below cannot
				// yet have destroyed anything arg3 needs, because arg3 is already done.
				// A future site that wants arg3 sourced from rdi/rsi, or arg1/arg2
				// sourced from rdx, needs the same read-before-clobber treatment this
				// comment (and the block below it) gives rdi/rsi.
				Emit(rdx, a_arg3);

				// Order matters as soon as a request names rdi or rsi as an argument
				// SOURCE. Materialising arg1 into rdi first would make a later
				// `Reg(RDI)` arg2 read back what we had just written (arg2 == arg1,
				// with no diagnostic). The register codes are plain ints in
				// LinuxLayout.h, so nothing stops a future site from pinning that.
				using From = CaveArg::From;
				const bool arg2FromRdi = a_arg2.from == From::kRegister && a_arg2.reg == Xbyak::Operand::RDI;
				const bool arg1FromRsi = a_arg1.from == From::kRegister && a_arg1.reg == Xbyak::Operand::RSI;
				if (arg2FromRdi && arg1FromRsi) {
					// Mutual swap. xchg does it in one instruction and touches no flags
					// (which are saved above in any case).
					xchg(rdi, rsi);
				} else if (arg2FromRdi) {
					Emit(rsi, a_arg2);  // read rdi before arg1 overwrites it
					Emit(rdi, a_arg1);
				} else {
					// Default order -- also correct when arg1 sources rsi, since arg1 is
					// read out of rsi before arg2 is written into it.
					Emit(rdi, a_arg1);
					Emit(rsi, a_arg2);
				}
			}

		private:
			void Emit(const Xbyak::Reg64& a_dst, const CaveArg& a_arg)
			{
				using namespace Xbyak::util;
				switch (a_arg.from) {
				case CaveArg::From::kRegister:
					mov(a_dst, Xbyak::Reg64(a_arg.reg));
					break;
				case CaveArg::From::kStackSlot:
					// rax points at the saved flags (see the prolog's alignment
					// bookkeeping), so hostRsp == rax + 16 and the host slot is at
					// rax + 16 + slot.
					mov(a_dst, ptr[rax + (16 + a_arg.slot)]);
					break;
				default:
					break;
				}
			}
		};

		// What, if anything, the epilog must re-materialise after restoring every
		// register — for a stolen window DKUtil's own replay path cannot take.
		enum class EpilogTail
		{
			kNone,
			// `mov rdi,rbp; xor esi,esi` — the UpdateCameraPitch ENTRY window.
			// Not replayed via kRestore*: `xor esi,esi` is opcode 0x31, outside
			// detail::DecodeStolenInsn's modelled set, so RelocateStolenBytes would
			// refuse the whole hook (CAVE-HOOKS.md §5's "never guess" behaviour).
			// Both instructions are position-independent, so re-emitting them is
			// exactly equivalent.
			kPitchEntry,
			// `mov rax, imm64(&ecl::EoCClient slot); mov rax,[rax]` — the
			// HandleCameraInput window, whose stolen instruction IS rip-relative
			// (`mov rax,[rip+0x5612aa4]`). Re-materialising it absolutely is
			// independent of where the trampoline landed, so it has neither of the
			// replay path's two failure modes (unmodelled encoding, >±2GB).
			kAbsoluteGlobalLoad
		};

		struct CaveEpilog : Xbyak::CodeGenerator
		{
			explicit CaveEpilog(EpilogTail a_tail = EpilogTail::kNone, std::uintptr_t a_imm = 0)
			{
				using namespace Xbyak::util;
				for (int i = 0; i < 16; ++i) {
					movups(Xbyak::Xmm(i), ptr[rsp + i * 16]);
				}
				add(rsp, 0x100);
				pop(r11);
				pop(r10);
				pop(r9);
				pop(r8);
				pop(rdi);
				pop(rsi);
				pop(rdx);
				pop(rcx);
				// Mirror of the prolog: `pop rsp` lands straight on the saved-FLAGS
				// slot (discarding the parity `sub rsp,8`), then the flags, then rax.
				pop(rsp);
				popfq();
				pop(rax);

				switch (a_tail) {
				case EpilogTail::kPitchEntry:
					mov(rdi, rbp);
					xor_(esi, esi);
					break;
				case EpilogTail::kAbsoluteGlobalLoad:
					mov(rax, a_imm);
					mov(rax, ptr[rax]);
					break;
				default:
					break;
				}
			}
		};

		// Keeps installed hook handles alive for the process lifetime.
		std::vector<std::unique_ptr<dku::Hook::CaveHookHandle>>& CaveHandles()
		{
			static std::vector<std::unique_ptr<dku::Hook::CaveHookHandle>> handles;
			return handles;
		}

		// Xbyak buffers must outlive the copy into the trampoline; keeping them for
		// the process lifetime is simplest and costs a few hundred bytes.
		std::vector<std::unique_ptr<Xbyak::CodeGenerator>>& AsmBuffers()
		{
			static std::vector<std::unique_ptr<Xbyak::CodeGenerator>> buffers;
			return buffers;
		}

		// A wildcard-free byte window that identifies a patch site (LinuxLayout.h
		// "Install-time byte gates").
		struct SiteSignature
		{
			const std::uint8_t* bytes{ nullptr };
			std::size_t         size{ 0 };
			std::ptrdiff_t      at{ 0 };  // start, relative to the site
		};

		template <std::size_t N>
		constexpr SiteSignature Sig(const std::uint8_t (&a_bytes)[N], std::ptrdiff_t a_at) noexcept
		{
			return SiteSignature{ a_bytes, N, a_at };
		}

		struct CaveRequest
		{
			std::string_view key;             // site-catalog key (relation: inlined_into)
			const char*      name;
			std::uintptr_t   expectedHostVA;  // unbiased; cross-checked against the catalog
			std::uintptr_t   expectedSiteVA;  // unbiased; cross-checked against the catalog
			bool             useGuestExit;    // take the site from the row's guest_exit
			std::size_t      stolen;          // bytes to overwrite at the site
			SiteSignature    signature;
			CaveArg          arg1;
			CaveArg          arg2;
			CaveArg          arg3;             // defaults to None(); only AfterUpdateCameraZoom uses it
			EpilogTail       tail;
			std::uintptr_t   tailImmVA;       // unbiased address for kAbsoluteGlobalLoad
			bool             restoreStolen;   // true -> kRestoreAfterEpilog
			bool             enabled;
			const char*      disabledReason;
		};

		bool InstallCaveHook(const CaveRequest& a_req, dku::Hook::FuncInfo a_func)
		{
			std::uintptr_t guestEntry = 0;
			std::uintptr_t guestExit = 0;
			const auto     host = Catalog::ResolveHost(a_req.key, guestEntry, guestExit);
			if (!host) {
				WARN("{} not hooked: no usable catalog row (this target has no Linux call site; it needs a cave)", a_req.name)
				return false;
			}

			// A cave is the mechanism for an INLINED target. If the row ever says
			// `equivalent`, the target has a real Linux function again and this is the
			// wrong hook form — refuse rather than carve into a function entry.
			const auto* row = Catalog::Lookup(a_req.key);
			if (!row || row->relation != Catalog::Relation::InlinedInto) {
				ERROR("{} not hooked: catalog relation is not `inlined_into` — a cave is only the right "
					  "mechanism for an inlined target",
					a_req.name)
				return false;
			}
			// A row that states its own window length must agree with the reviewed one.
			if (row->len != 0 && row->len != a_req.stolen) {
				ERROR("{} not hooked: catalog len {} != the reviewed window length {}",
					a_req.name, row->len, a_req.stolen)
				return false;
			}

			const auto base = dku::Hook::Module::get().base();
			const auto hostVA = host - base;
			if (a_req.expectedHostVA && hostVA != a_req.expectedHostVA) {
				ERROR("{} not hooked: catalog host {:X} != the reviewed host {:X} — the row moved, refusing to patch",
					a_req.name, hostVA, a_req.expectedHostVA)
				return false;
			}

			// The site comes from the CATALOG (guest_entry, or guest_exit for a
			// wrap-shaped target), and the reviewed constant in LinuxLayout.h is the
			// cross-check — not the other way round. A row that has drifted from the
			// review is refused rather than silently preferred.
			const auto site = a_req.useGuestExit ? guestExit : guestEntry;
			if (!site) {
				WARN("{} not hooked: the catalog row does not pin a {} (regenerate the site catalog)",
					a_req.name, a_req.useGuestExit ? "guest_exit" : "guest_entry")
				return false;
			}
			const auto siteVA = site - base;
			if (a_req.expectedSiteVA && siteVA != a_req.expectedSiteVA) {
				ERROR("{} not hooked: catalog {} {:X} != the reviewed window {:X} — refusing to patch",
					a_req.name, a_req.useGuestExit ? "guest_exit" : "guest_entry", siteVA, a_req.expectedSiteVA)
				return false;
			}
			if (!a_req.stolen) {
				WARN("{} not hooked: cave window length is unknown", a_req.name)
				return false;
			}

			if (!a_req.enabled) {
				WARN("{} cave hook is implemented but DISABLED (host {:X}, cave {:X}, {} bytes): {}",
					a_req.name, hostVA, siteVA, a_req.stolen, a_req.disabledReason)
				return false;
			}

			// Last gate before we write into the running program: the reviewed
			// instruction bytes must actually be there.
			if (a_req.signature.bytes &&
				!Catalog::BytesMatch(site, a_req.signature.at, a_req.signature.bytes, a_req.signature.size, a_req.name)) {
				return false;
			}

			auto prolog = std::make_unique<CaveProlog>(a_req.arg1, a_req.arg2, a_req.arg3);
			auto epilog = std::make_unique<CaveEpilog>(
				a_req.tail, a_req.tailImmVA ? base + a_req.tailImmVA : 0);

			const auto begin = static_cast<std::ptrdiff_t>(site - host);
			const auto end = begin + static_cast<std::ptrdiff_t>(a_req.stolen);

			// kRestoreAfterEpilog replays the stolen instructions from the trampoline
			// through detail::RelocateStolenBytes, which relocates a rip-relative
			// displacement and REFUSES the hook outright for any encoding it cannot
			// model (CAVE-HOOKS.md §5). Windows whose bytes that path cannot take are
			// re-materialised by the epilog tail instead, with no flag at all.
			const auto flag = a_req.restoreStolen ? dku::Hook::HookFlag::kRestoreAfterEpilog :
			                                        dku::Hook::HookFlag::kSkipNOP;

			auto handle = dku::Hook::AddCaveHook(host, { begin, end }, a_func,
				prolog.get(), epilog.get(), flag);
			if (!handle || handle->Address == 0) {
				ERROR("{} cave hook was refused (see the DKUtil ERROR above): host {:X}, cave {:X}",
					a_req.name, hostVA, siteVA)
				return false;
			}
			handle->Enable();
			CaveHandles().push_back(std::move(handle));
			AsmBuffers().push_back(std::move(prolog));
			AsmBuffers().push_back(std::move(epilog));
			INFO("Installed {} cave hook: host {:X}, cave {:X} (+{} bytes, stolen bytes {})",
				a_req.name, hostVA, siteVA, a_req.stolen,
				a_req.restoreStolen ? "replayed by DKUtil" : "re-materialised by the epilog")
			return true;
		}

		// -----------------------------------------------------------------
		// UpdateCameraPitch's raise/restore latch.
		//
		// The Windows hook is a WRAP: raise the camera definition's three
		// pitchAdjustSpeed fields to 100000, call the original, put them back. On
		// Linux the original is inlined, so the two halves live in two caves — the
		// ENTRY cave raises, the EXIT cave restores. This latch is what connects
		// them, and it stores the DEFINITION POINTER the entry actually wrote to,
		// so the restore cannot land on a different definition if the camera mode
		// changed in between.
		//
		// CAVEAT C5 from the review: if the intervening CalculateCameraPitch throws,
		// the exit cave never runs and the speeds would stay at 100000 forever. The
		// Windows wrapper has exactly the same exposure. Here the ENTRY cave calls
		// Restore() first, so the next frame recovers instead.
		// -----------------------------------------------------------------
		struct PitchSpeedLatch
		{
			RE::CameraDefinition* definition{ nullptr };
			float                 speedA{ 0.f };
			float                 speedB{ 0.f };
			float                 speedC{ 0.f };
			bool                  raised{ false };
		};

		PitchSpeedLatch g_pitchSpeeds;

		void RestorePitchSpeeds() noexcept
		{
			if (!g_pitchSpeeds.raised || !g_pitchSpeeds.definition) {
				g_pitchSpeeds.raised = false;
				g_pitchSpeeds.definition = nullptr;
				return;
			}
			g_pitchSpeeds.definition->pitchAdjustSpeedA_48 = g_pitchSpeeds.speedA;
			g_pitchSpeeds.definition->pitchAdjustSpeedB_F0 = g_pitchSpeeds.speedB;
			g_pitchSpeeds.definition->pitchAdjustSpeedC_F4 = g_pitchSpeeds.speedC;
			g_pitchSpeeds.raised = false;
			g_pitchSpeeds.definition = nullptr;
		}
	}  // namespace

	// =====================================================================
	// Cave callbacks.
	//
	// Plain `extern "C"` so the trampoline's `call [rip+disp]` reaches an
	// unmangled, non-inlinable entry point. Every one of them wraps its whole body
	// in `try { } catch (...) { }`: an exception escaping into the trampoline would
	// unwind through a frame with no CFI at all, in the middle of a game function.
	// (They are deliberately NOT declared `noexcept` — that would turn the same
	// event into std::terminate, and it would also make FUNC_INFO's argument-count
	// deduction depend on a noexcept function-pointer conversion.)
	// =====================================================================

	// (1a) ENTRY cave at 0x2c7f902 inside UpdateCamera 0x2c7d3a0.
	//
	// a_cameraObject = rbp (callee-saved across the intervening call to
	// CalculateCameraPitch; never written between 0x2c7f902 and 0x2c7f9a1).
	// a_deltaCtx    = [rsp+0x78], UpdateCamera's own a3 spill, stored once at
	//                 `mov [rsp+0x78],rdx`@0x2c7d3bb.
	extern "C" void NCT_Cave_UpdateCameraPitch_Entry(RE::CameraObject* a_cameraObject, const void* a_deltaCtx)
	{
		try {
			auto* cameraTweaks = CameraTweaks::GetSingleton();

			// deltaTime: tracker 0x141e0f120 §3.3 matches the read
			// instruction-for-instruction on both sides (`subss xmm0,[rbx+0x8]`
			// @0x141e0f3fa <-> `subss xmm1,[rax+0x8]`@0x2c7d523).
			if (a_deltaCtx) {
				cameraTweaks->SetDeltaTime(*reinterpret_cast<const float*>(
					reinterpret_cast<std::uintptr_t>(a_deltaCtx) + kUpdateCameraPitch_deltaTime));
			}

			// Recover from a previous pass whose exit cave never ran (caveat C5).
			RestorePitchSpeeds();

			if (!a_cameraObject) {
				return;
			}

			const auto currentPlayer = cameraTweaks->GetCurrentPlayer();
			const auto playerId = currentPlayer ? currentPlayer->playerId_38 : static_cast<int16_t>(1);
			cameraTweaks->SetCameraObjectForPlayer(playerId, a_cameraObject);

			if (!cameraTweaks->IsCameraUnlocked(playerId, a_cameraObject)) {
				return;
			}

			// GetCurrentCameraDefinition is inlined at every Linux call site
			// (tracker 0x141c714e0), so this is the mod's reimplementation — the
			// same one Hook_UpdateCameraPitch uses on Windows via the game function.
			auto* definition = Utils::GetCurrentCameraDefinition(a_cameraObject->cameraModeFlags);
			if (!definition) {
				static std::once_flag warned;
				WarnOnce(warned,
					"UpdateCameraPitch entry cave: no camera definition (ls::GlobalSwitches unresolved) — "
					"pitch-speed override not applied this frame");
				return;
			}

			g_pitchSpeeds.definition = definition;
			g_pitchSpeeds.speedA = definition->pitchAdjustSpeedA_48;
			g_pitchSpeeds.speedB = definition->pitchAdjustSpeedB_F0;
			g_pitchSpeeds.speedC = definition->pitchAdjustSpeedC_F4;
			g_pitchSpeeds.raised = true;

			definition->pitchAdjustSpeedA_48 = 100000.f;
			definition->pitchAdjustSpeedB_F0 = 100000.f;
			definition->pitchAdjustSpeedC_F4 = 100000.f;
		} catch (...) {
		}
	}

	// (1b) EXIT cave at 0x2c7f961 inside the same host — the other half of the
	// wrap. Takes no argument: it restores through the pointer the entry saved.
	extern "C" void NCT_Cave_UpdateCameraPitch_Exit()
	{
		try {
			RestorePitchSpeeds();
		} catch (...) {
		}
	}

	// (2) The HandleToggleInputMode gate.
	//
	// Reached only through the retargeted `je` at 0x3b90780, i.e. only when the
	// host has already established that the input id is 0xC0 (`cmp r14d,0xc0`
	// @0x3b90776, with r14d loaded from `mov r14d,[rbx]`@0x3b9070d — which is also
	// the proof that rbx is the InputID* and that the id sits at its offset 0).
	//
	// Returns true to BLOCK the toggle (the gate jumps to the guest's own
	// return-0 / forward-to-next-handler exit at 0x3b90786), false to allow it
	// (the gate jumps to the original `je` target 0x3b90a0a).
	//
	// This is the body of the Windows Hook_HandleToggleInputMode minus the call to
	// the original — which is exactly what the control-flow patch replaces.
	extern "C" bool NCT_ShouldBlockToggle(int16_t a_playerId, void* a_inputId)
	{
		try {
			// a_inputId (rbx) is passed for provenance and for future use; it is
			// deliberately NOT dereferenced. The site's own `cmp r14d,0xc0` already
			// establishes the id, and nothing else about this struct's Linux layout
			// has been through the review protocol.
			(void)a_inputId;

			auto* cameraTweaks = CameraTweaks::GetSingleton();
			if (cameraTweaks->ShouldSkipToggleInputMode(a_playerId)) {
				cameraTweaks->SetSkipToggleInputMode(a_playerId, false);
				return true;  // block
			}
		} catch (...) {
		}
		return false;  // allow
	}

	// (3) Cave at 0x25630a5 inside host 0x2562df0, for the inlined
	// HandleCameraInput body.
	//
	//   a_this        = r15
	//   a_inputEvent  = [rsp+0x18] (the a4 ls::InputID* / input event), homed once
	//                   at `mov [rsp+0x18],rdx`@0x2562e01.
	//   a3 is NOT readable here (scalarised; rbx/r14d path-dependent via the
	//   0x2563906 -> 0x25639f6 edge), so the camera comes from the mod's own
	//   tracked state instead — CameraTweaks::GetCurrentCamera(), fed by
	//   Hook_UpdateCamera through the dual-confirmed RE::UnkObject row 7.
	//
	// WHAT THIS CAVE CANNOT DO — the honest list.
	//
	// The Windows hook is a WRAP around a real call: several of its effects happen
	// AFTER the original returns, and a cave placed before the inlined body has no
	// "after" to run in. Specifically:
	//
	//   * The verdict byte. The host's return value is assembled at 0x2563419
	//     (byte 0 = al, byte 1 = bp), downstream of this cave. A cave here cannot
	//     change it. The Windows hook does not modify the return value either (it
	//     forwards whatever the original returned), so nothing observable is lost
	//     on that axis — but any FUTURE change that wanted to would need a second
	//     hook at the exit, not this one.
	//   * `cameraObject->zoomDelta *= <zoom mult>` and
	//     `cameraObject->currentAngleDelta *= <keyboard rotation mult>`, both of
	//     which Windows applies to a value the ORIGINAL produced. Reproduced here
	//     as a pre-scale of the corresponding INPUT value instead (see the two
	//     `SUBSTITUTION` notes below). That is equivalent only if the game derives
	//     the delta linearly from the input value; it has not been proven to.
	//
	// Everything else the Windows body does — the controller zoom-vs-pitch swap,
	// the deadzone adjustment, the mouse-rotation multiplier, the skip-toggle
	// latch — happens BEFORE the original on Windows too, and is reproduced here
	// exactly.
	extern "C" void NCT_Cave_HandleCameraInput(void* a_this, std::uintptr_t a_inputEvent)
	{
		try {
			(void)a_this;
			if (!a_inputEvent) {
				return;
			}
			if (!Offsets::bIsInControllerMode) {
				static std::once_flag warned;
				WarnOnce(warned,
					"HandleCameraInput cave: bIsInControllerMode unresolved — input adjustment is off");
				return;
			}

			const auto inputId = *reinterpret_cast<Offsets::InputID*>(a_inputEvent + kHandleCameraInput_inputId);
			const bool bIsInControllerMode = *Offsets::bIsInControllerMode;

			auto* settings = Settings::Main::GetSingleton();
			auto* cameraTweaks = CameraTweaks::GetSingleton();
			// Row 7 of RE::UnkObject (Win +0x30 -> Lin +0x00), dual-confirmed. NOT
			// currentCameraObject2 (Lin +0x60), which the Windows body uses: that
			// field is not an RE::CameraObject at all on either side (stride 0x1c,
			// byte flag at +0x8 — see LinuxLayout.h) and no Linux path may cast it.
			auto* cameraObject = cameraTweaks->GetCurrentCamera();

			switch (inputId) {
			case Offsets::InputID::kZoomIn:
			case Offsets::InputID::kZoomOut:
				{
					float* pInputValue = reinterpret_cast<float*>(a_inputEvent + kHandleCameraInput_inputValueA);

					// bControllerZoomAvailable proves the four SLOTS resolved; the objects
					// they point at can still be null before the engine has built them.
					// Windows has the same exposure, but there a wrong guess only ever fed
					// the game its own pointer back — here GetInputValueLinux would be
					// handed a null `this`.
					const bool bControllerChainReady =
						Offsets::bControllerZoomAvailable &&
						*Offsets::UnkSingletonPtr != nullptr &&
						*Offsets::UnkPlayerSingletonPtr != nullptr &&
						*Offsets::UnkInputSingletonPtr != nullptr;

					if (bIsInControllerMode && bControllerChainReady && cameraObject) {
						const auto currentPlayer = cameraTweaks->GetCurrentPlayer();
						const auto playerId = currentPlayer ? currentPlayer->playerId_38 : static_cast<int16_t>(1);

						const bool bCanAdjustPitch = cameraTweaks->CanAdjustPitch(cameraObject);
						bool       bDoZoom;
						bool       bUseRightStickPressForZoom;
						bool       bSwapZoomAndPitch;
						{
							ReadLocker locker(settings->Lock);
							bUseRightStickPressForZoom = *settings->UseRightStickPressForZoom;
							bSwapZoomAndPitch = *settings->SwapZoomAndPitch;
						}

						if (!bCanAdjustPitch) {
							bDoZoom = true;
						} else if (bUseRightStickPressForZoom) {
							// ShouldShowSneakCones has no standalone Linux function;
							// this is the mod's reimplementation of it.
							bDoZoom = Offsets::ShouldShowSneakConesLinux(*Offsets::UnkSingletonPtr, playerId);
						} else {
							// GetPlayerController is inlined on Linux — reimplemented
							// lookup. It returns the BYTE the Linux GetInputValue
							// wants in dl (0xFF = no controller for this player), not
							// an opaque pointer: the Linux callee is
							// `f(manager, int32 id, uint8 playerIndex)` and tests
							// `cmp dl,0xff` at its first branch (0x2d1d883).
							const auto playerController =
								Offsets::LookupPlayerController(*Offsets::UnkPlayerSingletonPtr, playerId);
							const RE::InputValue inputValue = Offsets::GetInputValueLinux(
								*Offsets::UnkInputSingletonPtr,
								static_cast<int32_t>(Offsets::InputID::kToggleInputMode),
								playerController);
							bDoZoom = inputValue.bIsPressed;
						}

						if (bCanAdjustPitch && bSwapZoomAndPitch) {
							bDoZoom = !bDoZoom;
						}

						const bool bShouldSkipToggleInputMode = bSwapZoomAndPitch ? !bDoZoom : bDoZoom;
						if (bCanAdjustPitch && bShouldSkipToggleInputMode && !bUseRightStickPressForZoom &&
							*pInputValue > CameraTweaks::VANILLA_DEADZONE) {
							// zooming with ToggleInputMode held: swallow the next press
							cameraTweaks->SetSkipToggleInputMode(playerId, true);
						}

						if (bDoZoom) {
							cameraTweaks->SetControllerPitchDelta(playerId, 0.f);
						} else {
							// do pitch instead of zoom.
							//
							// This is the ONLY new write into live game memory this cave
							// adds, and RE::CameraObject::zoomDelta (+0xA4) is a
							// Windows-derived offset that has not been through the review
							// protocol on its own. It is corroborated, not proven: this
							// host reads `movss xmm0,DWORD PTR [r15+0xa4]`@0x2c7fe24 with
							// r15 the CameraObject it stores at `mov [rsp+0x8],r15`
							// @0x2c7d444, and every other RE::CameraObject offset that HAS
							// been checked is delta-zero Win<->Lin (+0x18/+0x20/+0x24/+0x2c/
							// +0x254 matched instruction-for-instruction at the
							// AfterUpdateCameraZoom join, 0x141e0fba4 <-> 0x2c7fe46;
							// +0x14c/+0x164 at the pitch cave; +0x5c/+0x160 per the
							// SetDefaultZoom review; +0x168..+0x170 per the
							// AfterUpdateCameraZoom signature's 17<->17 access count).
							cameraObject->zoomDelta = 0.f;
							const float sign = inputId == Offsets::InputID::kZoomIn ? -1.f : 1.f;
							cameraTweaks->SetControllerPitchDelta(playerId, *pInputValue * sign);
							*pInputValue = 0.f;
							return;
						}
					}

					// SUBSTITUTION (see the header comment): Windows scales
					// `cameraObject->zoomDelta` AFTER the original produced it. That
					// point is downstream of this cave, so the multiplier is applied
					// to the input value the game is about to consume instead.
					{
						ReadLocker locker(settings->Lock);
						*pInputValue *= bIsInControllerMode ? static_cast<float>(*settings->ControllerZoomMult) :
						                                      static_cast<float>(*settings->MouseZoomMult);
					}
					static std::once_flag zoomSubstitution;
					WarnOnce(zoomSubstitution,
						"HandleCameraInput cave: zoom multiplier is applied to the INPUT VALUE, not to "
						"cameraObject->zoomDelta as on Windows (the post-call point is downstream of the cave). "
						"Equivalent only if the engine's zoom delta is linear in the input value — needs in-game "
						"validation");
					break;
				}
			case Offsets::InputID::kRotateLeft:
			case Offsets::InputID::kRotateRight:
				{
					float* pInputValue = reinterpret_cast<float*>(a_inputEvent + kHandleCameraInput_inputValueA);
					if (bIsInControllerMode) {
						// Windows applies this BEFORE the original too — exact.
						*pInputValue = cameraTweaks->AdjustInputValueForDeadzone(*pInputValue);
					} else {
						// SUBSTITUTION: Windows scales `cameraObject->currentAngleDelta`
						// after the original.
						{
							ReadLocker locker(settings->Lock);
							*pInputValue *= static_cast<float>(*settings->KeyboardCameraRotationMult);
						}
						static std::once_flag rotationSubstitution;
						WarnOnce(rotationSubstitution,
							"HandleCameraInput cave: keyboard rotation multiplier is applied to the INPUT VALUE, "
							"not to cameraObject->currentAngleDelta as on Windows — needs in-game validation");
					}
					break;
				}
			case Offsets::InputID::kMouseRotateLeft:
			case Offsets::InputID::kMouseRotateRight:
				{
					// Windows applies this before the original — exact.
					ReadLocker locker(settings->Lock);
					float* pInputValue = reinterpret_cast<float*>(a_inputEvent + kHandleCameraInput_inputValueB);
					*pInputValue *= static_cast<float>(*settings->MouseCameraRotationMult);
					break;
				}
			default:
				break;
			}
		} catch (...) {
		}
	}

	// (4) Cave at the AfterUpdateCameraZoom guest EXIT 0x2c7fe41 inside
	// UpdateCamera 0x2c7d3a0 — the join point where the Windows `ret` from
	// 0x141e1b5a0 lands.
	//
	//   a_a1          = [rsp+0x18], UpdateCamera's own arg1 (stored once at
	//                   `mov [rsp+0x18],rdi`@0x2c7d3c0).
	//   a_cameraObject= [rsp+0x8] rather than rbp: the displaced instruction is
	//                   what DEFINES rbp, so at the cave point rbp is not yet the
	//                   CameraObject. The slot is written once
	//                   (`mov [rsp+0x8],r15`@0x2c7d444) and read 20x including
	//                   here — the same value, one instruction earlier.
	//   a_a2          = [rsp+0x38] — pinned CONFIRMED 2026-09-04 (this row's
	//                   STATUS.md Attempt 1): UpdateCamera's own stack local,
	//                   written once at `mov [rsp+0x38],r13`@0x2c7d43f and still
	//                   unmodified at the guest exit.
	//
	// Both LinuxLayout gate numbers are now pinned — the a_a2 frame slot
	// (kCave_AfterUpdateCameraZoom_Arg2Slot = 0x38, CONFIRMED 2026-09-04) and
	// LinuxLayout::kZoomAdjustContext_floorQueryArg (= 0x110, CONFIRMED
	// 2026-09-07, win a1+0x118 <-> lin a1+0x110, tracker
	// 0x141e1b5a0-after-update-camera-zoom) — so this cave now wires the real
	// call. CameraTweaksManager.cpp's own `Known(kZoomAdjustContext_floorQueryArg)`
	// / `GetFloorLevelLinux` gate remains as the last defensive check before any
	// game memory is dereferenced.
	extern "C" void NCT_Cave_AfterUpdateCameraZoom(std::uintptr_t a_a1, RE::CameraObject* a_cameraObject, std::uintptr_t a_a2)
	{
		try {
			if (!a_cameraObject) {
				return;
			}

			bool bUnlockedPitchLimitClipping;
			{
				const auto settings = Settings::Main::GetSingleton();
				ReadLocker locker(settings->Lock);
				bUnlockedPitchLimitClipping = *settings->UnlockedPitchLimitClipping;
			}
			if (!bUnlockedPitchLimitClipping) {
				return;
			}

			// CameraTweaks::AdjustCameraZoomForPitch itself re-checks GetFloorLevelLinux
			// and Known(kZoomAdjustContext_floorQueryArg) (CameraTweaksManager.cpp) and
			// WARN-onces + no-ops if either is somehow still unmet at runtime — this
			// cave does not duplicate that check, only the null-cameraObject guard
			// every other NCT cave performs before touching game memory.
			CameraTweaks::GetSingleton()->AdjustCameraZoomForPitch(a_a1, a_a2, a_cameraObject);
		} catch (...) {
		}
	}

	uint8_t Offsets::LookupPlayerController(void* a_map, int16_t a_playerId) noexcept
	{
		using namespace NCT::LinuxLayout;
		// Verbatim reimplementation of Windows 0x143baaeb0, which clang inlined into
		// 0x25c6cd0 (guest body 0x25c6df8-0x25c6e32). Every offset comes from
		// LinuxLayout; both builds agree on all of them.
		if (!a_map) {
			return kPlayerControllerAbsent;
		}
		const auto mapBase = reinterpret_cast<std::uintptr_t>(a_map);

		// `mov r8d,[rdi+0x34]`@0x25c6dff ; `test r8,r8; je`@0x25c6e03
		const std::uint64_t bucketCount = *reinterpret_cast<const std::uint32_t*>(mapBase + kPlayerControllerMap_bucketCount);
		if (bucketCount == 0) {
			return kPlayerControllerAbsent;
		}
		// `add rdx,[rdi+0x38]`@0x25c6e18
		const auto buckets = *reinterpret_cast<const std::uintptr_t*>(mapBase + kPlayerControllerMap_buckets);
		if (!buckets) {
			return kPlayerControllerAbsent;
		}

		// Windows sign-extends the int16 key before dividing (`movsx r9,dx`); the
		// bucket comparison then uses the low 16 bits (`cmp r9w,[rax+8]`).
		const auto key = static_cast<std::uint64_t>(static_cast<std::int64_t>(a_playerId));
		const auto index = key % bucketCount;

		// `mov rdx,[rdx]`@0x25c6e1c ; `cmp WORD[rdx+8],si`@0x25c6e28 ; `jne`@0x25c6e2c
		auto node = *reinterpret_cast<const std::uintptr_t*>(buckets + index * sizeof(std::uintptr_t));
		while (node) {
			if (*reinterpret_cast<const std::uint16_t*>(node + kPlayerControllerNode_key) == static_cast<std::uint16_t>(a_playerId)) {
				// `cmp BYTE[rdx+0x10],0xff`@0x25c6e2e
				return *reinterpret_cast<const std::uint8_t*>(node + kPlayerControllerNode_value);
			}
			node = *reinterpret_cast<const std::uintptr_t*>(node + kPlayerControllerNode_next);
		}
		return kPlayerControllerAbsent;
	}

	// Reimplementation of Windows ShouldShowSneakCones (0x14182acc0). There is no
	// standalone Linux counterpart anywhere in .text — tracker
	// 0x14182acc0-should-show-sneak-cones, reviewer opus-5 CONFIRMED 2026-09-03
	// with an exhaustive absence proof (two genuine inlined copies, at host
	// 0x2d1dad0 body 0x2d1daf0 and host 0x2d21e60 body 0x2d2283a).
	//
	// Every step below cites the Linux instruction it reproduces (from
	// `bg3re disasm 0x2d1daf0 --around 45`, the first inlined copy) and its Windows
	// twin (`bg3re disasm 0x14182acc0 --around 50`). The two are identical in shape
	// AND in every offset; only the register allocation differs.
	//
	// a_table is `*UnkSingletonPtr` — the object the Windows call site passes in
	// rcx, and what the Linux copy loads with `mov rdi,[rip+0x4ee8703] # 7c061e8`
	// @0x2d1dade.
	bool Offsets::ShouldShowSneakConesLinux(void* a_table, int16_t a_playerId) noexcept
	{
		using namespace NCT::LinuxLayout;

		// Guard the DEREFERENCED singletons, not just the slot pointers: both are
		// read through below (`*UnkPlayerSingletonPtr` feeds LookupPlayerController,
		// `*UnkInputSingletonPtr` becomes GetInputValueLinux's `this`), and the
		// callee at 0x2d1d880 dereferences that `this` at
		// `mov r8d,[rdi+rcx*1+0x154]`@0x2d1d894 for any non-0xff player byte. The
		// slot pointers themselves are non-null whenever the catalog resolved them,
		// so testing those alone guards nothing. The sole caller already checks the
		// dereferenced form (bControllerChainReady); this makes the standalone
		// reimplementation safe on its own terms for the next caller.
		if (!a_table || !GetInputValueLinux ||
			!UnkPlayerSingletonPtr || !*UnkPlayerSingletonPtr ||
			!UnkInputSingletonPtr || !*UnkInputSingletonPtr) {
			return false;
		}
		const auto tableBase = reinterpret_cast<std::uintptr_t>(a_table);

		// --- first table: a PRESENCE test, no value byte on either side ---
		// Lin `mov r8d,[rdi+0x24]`@0x2d1daf0  <->  Win `mov eax,[rcx+0x24]`@0x14182acc4
		const std::uint64_t bucketCount =
			*reinterpret_cast<const std::uint32_t*>(tableBase + kSneakConeMap_bucketCount);
		// Lin `test r8,r8; je 0x2d1db75`@0x2d1dafc  <->  Win `test eax,eax; je`@0x14182accb
		if (bucketCount == 0) {
			return false;
		}
		// Lin `movsx rcx,si`@0x2d1db01  <->  Win `movsx r9,dx`@0x14182acc7
		const auto key = static_cast<std::uint64_t>(static_cast<std::int64_t>(a_playerId));
		// Lin `div r8`@0x2d1db0a + `shl rdx,3`@0x2d1db0d + `add rdx,[rdi+0x28]`@0x2d1db11
		// Win `div r8`@0x14182acd7 + `mov rax,[rcx+0x28]`@0x14182acda + `mov rcx,[rax+r8*8]`@0x14182ace1
		const auto buckets = *reinterpret_cast<const std::uintptr_t*>(tableBase + kSneakConeMap_buckets);
		if (!buckets) {
			return false;
		}
		// Lin `mov rdx,[rdx]`@0x2d1db15 ; `test rdx,rdx; je`@0x2d1db18
		auto node = *reinterpret_cast<const std::uintptr_t*>(buckets + (key % bucketCount) * sizeof(std::uintptr_t));
		for (;;) {
			if (!node) {
				// Lin `je 0x2d1db75`@0x2d1db1b  <->  Win `xor al,al; ret`@0x14182acff
				return false;
			}
			// Lin `cmp WORD PTR [rdx+0x8],si`@0x2d1db1d  <->  Win `cmp r9w,WORD PTR [rcx+0x8]`@0x14182acf0
			if (*reinterpret_cast<const std::uint16_t*>(node + kSneakConeNode_key) == static_cast<std::uint16_t>(a_playerId)) {
				break;  // Lin falls through to 0x2d1db23 / Win `je 0x14182ad06`
			}
			// Lin `jne 0x2d1db15`@0x2d1db21  <->  Win `mov rcx,[rcx]`@0x14182acf7
			node = *reinterpret_cast<const std::uintptr_t*>(node + kSneakConeNode_next);
		}

		// --- second table: the controller sub-id, default 0xFF ---
		// Lin 0x2d1db23 (`mov rdi,[rip] # 7d5c038`) .. 0x2d1db54 (`mov r8b,[rdx+0x10]`)
		// is byte-for-byte the standalone walk at 0x25c6df8..0x25c6e2e that
		// LookupPlayerController already implements: +0x34 count, +0x38 buckets,
		// +0x8 u16 key, +0x10 u8 value, `mov r8b,0xff`@0x2d1db2a for absent.
		// Win 0x14182ad06..0x14182ad3f is the same walk.
		const auto playerController = LookupPlayerController(*UnkPlayerSingletonPtr, a_playerId);

		// --- the call ---
		// Lin `mov rdi,[rip] # 7d9d0a8`@0x2d1db58 ; `movsx edx,r8b`@0x2d1db5f ;
		//     `mov esi,0xdc`@0x2d1db63 ; `call 2d1d880`@0x2d1db68 ; `test al,al`@0x2d1db6d
		// Win `mov rcx,[rip] # 0x1462d49a0`@0x14182ad42 ; `mov [rsp+0x40],0xdc`@0x14182ad53 ;
		//     `call 0x144125ce0`@0x14182ad5b ; `movzx eax,BYTE PTR [rax+8]`@0x14182ad60
		//
		// The Windows `[rax+8]` is the out-param's third member; the Linux callee
		// returns the same aggregate BY VALUE in xmm0:rax, so RE::InputValue's
		// `bIsPressed` IS that byte (static_asserts in Hooks.h pin the SysV
		// classification).
		const RE::InputValue inputValue = GetInputValueLinux(
			*UnkInputSingletonPtr, kInputAction_ShowSneakCones, playerController);
		return inputValue.bIsPressed;
	}

	namespace
	{
		// -----------------------------------------------------------------
		// (2) HandleToggleInputMode: a rel32 retarget, NOT a cave.
		//
		// Nothing is stolen and no instruction is displaced. The site is the 6-byte
		// `je 0x3b90a0a` at 0x3b90780 (`0F 84 84 02 00 00`) and ONLY its rel32
		// operand is rewritten, to point at a gate stub in the trampoline. The
		// `r14d != 0xc0` fallthrough into 0x3b90786 stays byte-for-byte identical,
		// so the gate runs exclusively on the ToggleInputMode path.
		//
		// The gate (all of it verified against `bg3re disasm 0x3b90760 --end
		// 0x3b907b0` and `... 0x3b90a00 --end 0x3b90a20`):
		//
		//   sub rsp,8 ; push rdi     rsp is 0 (mod 16) throughout this host's body
		//                            (prologue = 6 pushes + `sub rsp,0x98` = 208
		//                            bytes, 208 % 16 == 0), so this pair keeps the
		//                            `call` 16-byte aligned. rdi is the ONLY
		//                            caller-saved register live across the site —
		//                            it is consumed by the first instruction of the
		//                            allow target (`mov [rsp+0x8],rdi`@0x3b90a0a).
		//   movzx edi,word [r15]     playerId. `add r15,0x168`@0x3b90766 has already
		//                            run, so r15 == a1 + kHandleToggleInputMode_playerId.
		//                            The host itself reads it the same way two
		//                            instructions into the allow path
		//                            (`movsx esi,WORD PTR [r15]`@0x3b90a16); movzx
		//                            vs movsx is immaterial for an int16_t argument,
		//                            whose high bits SysV leaves unspecified.
		//   mov rsi,rbx              the InputID* (set at 0x3b905f8, still read at
		//                            `cmp [rbx+0x1c],0`@0x3b90a3f, and rbx is
		//                            callee-saved across the intervening call at
		//                            0x3b9075e).
		//   call NCT_ShouldBlockToggle
		//   pop rdi ; add rsp,8      restore BEFORE the flag test: `add` writes
		//                            EFLAGS, `pop` does not, and neither touches al.
		//   test al,al
		//   jne  -> mov rax,BLOCK ; jmp rax
		//   jmp  -> mov rax,ALLOW ; jmp rax
		//
		// Both exits go through rax because an absolute `mov rax,imm64; jmp rax` has
		// no ±2GB range condition at all. rax is dead at both targets: 0x3b90a0a
		// begins `mov [rsp+0x8],rdi` / `mov rdi,[rip]` / `movsx esi,word [r15]` and
		// only then calls; 0x3b90786 begins `mov rax,[r14+0x88]`, which defines it.
		// EFLAGS are dead at both (each begins with a mov), and the whole
		// 0x3b905e0..0x3b90f90 host touches xmm exactly twice (0x3b90aee/0x3b90afe,
		// defining xmm0 from r13), so no XMM register is live here either.
		//
		// The block exit is 0x3b90786 — the guest's own return-0 /
		// forward-to-next-handler exit — NOT 0x3b90a06, which is the
		// next-handler-is-NULL path. That one token was the review's single
		// rejection of the earlier design.
		// -----------------------------------------------------------------
		struct ToggleGate : Xbyak::CodeGenerator
		{
			ToggleGate(std::uintptr_t a_callback, std::uintptr_t a_allow, std::uintptr_t a_block)
			{
				using namespace Xbyak::util;
				Xbyak::Label blockLabel;

				sub(rsp, 8);
				push(rdi);
				movzx(edi, word[r15]);
				mov(rsi, rbx);
				mov(rax, a_callback);
				call(rax);
				pop(rdi);
				add(rsp, 8);
				test(al, al);
				jne(blockLabel, T_NEAR);

				mov(rax, a_allow);
				jmp(rax);

				L(blockLabel);
				mov(rax, a_block);
				jmp(rax);
			}
		};

		bool InstallToggleGate()
		{
			if (!kPatch_HandleToggleInputMode_Enabled) {
				WARN("HandleToggleInputMode gate is implemented but DISABLED in LinuxLayout.h")
				return false;
			}

			std::uintptr_t guestEntry = 0;
			const auto     host = Catalog::ResolveHost(Keys::HandleToggleInputMode, guestEntry);
			if (!host) {
				WARN("HandleToggleInputMode not patched: no usable catalog row")
				return false;
			}
			if (!Catalog::KindOk(Keys::HandleToggleInputMode, Catalog::Kind::PatchSite)) {
				return false;
			}

			const auto base = dku::Hook::Module::get().base();
			if (host - base != kPatch_HandleToggleInputMode_HostVA) {
				ERROR("HandleToggleInputMode not patched: catalog host {:X} != the reviewed host {:X}",
					host - base, kPatch_HandleToggleInputMode_HostVA)
				return false;
			}

			// The catalog's own `len` for this row is the patch length; cross-check
			// it. (Its `patch_site` field is NOT part of DKUtil's Site struct — the
			// row's guest_entry is the RAW inlined-body start 0x3b906b0, not the
			// patch placement — so the site address itself comes from LinuxLayout.h,
			// gated by the byte signature below.)
			if (const auto* row = Catalog::Lookup(Keys::HandleToggleInputMode);
				row && row->len != 0 && row->len != kPatch_HandleToggleInputMode_SiteLen) {
				ERROR("HandleToggleInputMode not patched: catalog len {} != the reviewed len {}",
					row->len, kPatch_HandleToggleInputMode_SiteLen)
				return false;
			}

			const auto site = base + kPatch_HandleToggleInputMode_SiteVA;
			const auto allow = base + kPatch_HandleToggleInputMode_AllowVA;
			const auto block = base + kPatch_HandleToggleInputMode_BlockVA;

			if (!Catalog::BytesMatch(site, kSig_HandleToggleInputMode_SiteAt,
					kSig_HandleToggleInputMode_Site, sizeof(kSig_HandleToggleInputMode_Site),
					"HandleToggleInputMode gate")) {
				return false;
			}

			auto gate = std::make_unique<ToggleGate>(
				reinterpret_cast<std::uintptr_t>(&NCT_ShouldBlockToggle), allow, block);

			const auto  size = gate->getSize();
			const auto  dst = AsAddress(dku::Hook::Trampoline::Allocate(size));
			if (!dst) {
				ERROR("HandleToggleInputMode not patched: no trampoline space for the {}-byte gate", size)
				return false;
			}

			// rel32 is measured from the END of the 6-byte `je`.
			const auto end = site + kPatch_HandleToggleInputMode_SiteLen;
			const auto disp = static_cast<std::ptrdiff_t>(dst) - static_cast<std::ptrdiff_t>(end);
			if (disp < (std::numeric_limits<std::int32_t>::min)() || disp > (std::numeric_limits<std::int32_t>::max)()) {
				ERROR("HandleToggleInputMode not patched: gate at {:X} is {} bytes from the site — outside rel32 range",
					dst, disp)
				return false;
			}

			dku::Hook::WriteData(dst, gate->getCode(), size, false);
			const auto rel = static_cast<std::int32_t>(disp);
			dku::Hook::WriteData(site + kPatch_HandleToggleInputMode_Rel32At, &rel, sizeof(rel), false);

			AsmBuffers().push_back(std::move(gate));
			INFO("Patched HandleToggleInputMode: je rel32 at {:X} retargeted to a {}-byte gate at {:X} "
				 "(allow {:X}, block {:X})",
				kPatch_HandleToggleInputMode_SiteVA, size, dst - base,
				kPatch_HandleToggleInputMode_AllowVA, kPatch_HandleToggleInputMode_BlockVA)
			return true;
		}
	}  // namespace

	bool Hooks::HookLinux()
	{
		using namespace NCT::LinuxLayout;
		bool bSuccess = true;

		// --- targets that survived as real Linux functions with a live call site ---

		_UpdateCamera = WriteCallSite(Keys::UpdateCamera, "UpdateCamera", Hook_UpdateCamera);
		bSuccess = _UpdateCamera != nullptr && bSuccess;

		_CalculateCameraPitch = WriteCallSite(Keys::CalculateCameraPitch, "CalculateCameraPitch", Hook_CalculateCameraPitch);
		bSuccess = _CalculateCameraPitch != nullptr && bSuccess;

		_SetDefaultZoom = WriteCallSite(Keys::SetDefaultZoom, "SetDefaultZoom", Hook_SetDefaultZoom);
		bSuccess = _SetDefaultZoom != nullptr && bSuccess;

		// --- targets clang inlined: no call site exists to rewrite ---
		//
		// The argument sources live in LinuxLayout.h with the evidence that makes
		// each one sound at its OWN site; these assertions keep the plain ints there
		// honest without making that header depend on Xbyak.
		static_assert(kCave_UpdateCameraPitch_ArgReg == Xbyak::Operand::RBP);
		static_assert(kCave_HandleCameraInput_ArgReg == Xbyak::Operand::R15);
		static_assert(kPatch_HandleToggleInputMode_PlayerIdPtrReg == Xbyak::Operand::R15);
		static_assert(kPatch_HandleToggleInputMode_InputIdReg == Xbyak::Operand::RBX);

		// (1) UpdateCameraPitch — a WRAP, so TWO caves in the same host.
		const auto pitchHandlesBefore = CaveHandles().size();
		const bool pitchEntry = InstallCaveHook(
			CaveRequest{
				Keys::UpdateCameraPitch, "UpdateCameraPitch (entry)",
				kCave_UpdateCameraPitch_HostVA, kCave_UpdateCameraPitch_EntryVA,
				/*useGuestExit*/ false, kCave_UpdateCameraPitch_EntryStolen,
				Sig(kSig_UpdateCameraPitch_Entry, kSig_UpdateCameraPitch_EntryAt),
				CaveArg::Reg(kCave_UpdateCameraPitch_ArgReg),
				CaveArg::Slot(kCave_UpdateCameraPitch_DeltaCtxSlot),
				CaveArg::None(),
				EpilogTail::kPitchEntry, 0,
				/*restoreStolen*/ false,
				kCave_UpdateCameraPitch_Enabled, "" },
			FUNC_INFO(NCT_Cave_UpdateCameraPitch_Entry));

		const bool pitchExit = InstallCaveHook(
			CaveRequest{
				Keys::UpdateCameraPitch, "UpdateCameraPitch (exit)",
				kCave_UpdateCameraPitch_HostVA, kCave_UpdateCameraPitch_ExitVA,
				/*useGuestExit*/ true, kCave_UpdateCameraPitch_ExitStolen,
				Sig(kSig_UpdateCameraPitch_Exit, kSig_UpdateCameraPitch_ExitAt),
				CaveArg::None(), CaveArg::None(), CaveArg::None(),
				EpilogTail::kNone, 0,
				// `mov rax,[rsp+0x78]` is rsp-relative and inside the decoder's
				// modelled set, so DKUtil replays it AFTER the epilog has fully
				// restored rsp (and after `pop rax`) — it lands in rax with the
				// displacement unchanged, ready for `mulss xmm4,[rax+0xc]`@0x2c7f973.
				/*restoreStolen*/ true,
				kCave_UpdateCameraPitch_Enabled, "" },
			FUNC_INFO(NCT_Cave_UpdateCameraPitch_Exit));

		// Half a wrap is worse than none: if the entry raised the pitch speeds and
		// the exit is not there to put them back, they stay at 100000 forever.
		if (pitchEntry != pitchExit) {
			ERROR("UpdateCameraPitch: only {} of the two caves installed — the pitch-speed override is a "
				  "raise/restore PAIR and must never run half-installed. Reverting it.",
				pitchEntry ? "the entry" : "the exit")
			// Revert ONLY what this pair installed (the two calls above are the first
			// caves HookLinux() attempts, but index from the recorded size rather than
			// relying on that staying true).
			auto& handles = CaveHandles();
			for (auto i = handles.size(); i > pitchHandlesBefore; --i) {
				if (handles[i - 1]) {
					handles[i - 1]->Disable();
				}
				handles.pop_back();
			}
			bSuccess = false;
		}

		// (3) HandleCameraInput.
		InstallCaveHook(
			CaveRequest{
				Keys::HandleCameraInput, "HandleCameraInput",
				kCave_HandleCameraInput_HostVA, kCave_HandleCameraInput_CaveVA,
				/*useGuestExit*/ false, kCave_HandleCameraInput_Stolen,
				Sig(kSig_HandleCameraInput_Cave, kSig_HandleCameraInput_CaveAt),
				CaveArg::Reg(kCave_HandleCameraInput_ArgReg),
				CaveArg::Slot(kCave_HandleCameraInput_InputEventSlot),
				CaveArg::None(),
				// The stolen instruction IS rip-relative; do not let DKUtil replay
				// it. The epilog re-materialises the load absolutely instead.
				EpilogTail::kAbsoluteGlobalLoad, kCave_HandleCameraInput_EoCClientVA,
				/*restoreStolen*/ false,
				kCave_HandleCameraInput_Enabled, "" },
			FUNC_INFO(NCT_Cave_HandleCameraInput));

		// (4) AfterUpdateCameraZoom, at the guest EXIT.
		InstallCaveHook(
			CaveRequest{
				Keys::AfterUpdateCameraZoom, "AfterUpdateCameraZoom",
				kCave_AfterUpdateCameraZoom_HostVA, kCave_AfterUpdateCameraZoom_CaveVA,
				/*useGuestExit*/ false, kCave_AfterUpdateCameraZoom_Stolen,
				Sig(kSig_AfterUpdateCameraZoom, kSig_AfterUpdateCameraZoomAt),
				CaveArg::Slot(kCave_AfterUpdateCameraZoom_Arg1Slot),
				CaveArg::Slot(kCave_AfterUpdateCameraZoom_CameraObjectSlot),
				CaveArg::Slot(kCave_AfterUpdateCameraZoom_Arg2Slot),
				EpilogTail::kNone, 0,
				// `mov rbp,[rsp+0x8]` is rsp-relative and modelled; replayed after
				// the epilog restores rsp, so it lands in rbp unchanged.
				/*restoreStolen*/ true,
				kCave_AfterUpdateCameraZoom_Enabled, "" },
			FUNC_INFO(NCT_Cave_AfterUpdateCameraZoom));

		// (2) HandleToggleInputMode — a control-flow patch, not a cave.
		InstallToggleGate();

		// --- SDL mouse delta ---
		// tracker 0x1441280d0-sdl-mouse-y: obviated, mechanism `interpose`. The
		// Windows path (an Xbyak stub feeding r12d into a 4th argument, then
		// write_call<5>) has no Linux counterpart. src/Linux/SdlInput.cpp installs
		// an SDL_MOUSEMOTION watch that writes yrel into CameraTweaks::delta_y
		// directly; main.cpp calls its installer.
		INFO("SDLMouseYHook: Windows call-site path skipped on Linux — NCT::Linux::InstallMouseYWatch() supplies delta_y")

		INFO("Linux hook installation done: {} trampoline bytes used of {}",
			dku::Hook::Trampoline::GetTrampoline().consumed(),
			dku::Hook::Trampoline::GetTrampoline().capacity())

		return bSuccess;
	}
#endif  // !_WIN32

	void Install()
	{
		Offsets::Init();
		Hooks::Hook();
	}

    void Hooks::Hook_UpdateCamera(uint64_t a1, uint64_t a2, uint64_t a3, RE::UnkObject* a4)
	{
#if !defined(_WIN32)
		// SetCameraSettings() reads nothing out of a4 — it works entirely through
		// Offsets::UnkCameraSingletonPtr (ls::GlobalSwitches, which resolves) — so
		// it must NOT sit behind the a4-layout gate below. Keeping it there held the
		// mod's main feature hostage to offsets it never touches.
		CameraTweaks::GetSingleton()->SetCameraSettings();

		// tracker 0x141e0f120-update-camera: "by-value a4 struct is
		// member-reordered". §3.2 rows 3 and 7 pin the two fields this hook reads
		// (dual-confirmed, consumer witnesses on both sides — see LinuxLayout.h), so
		// this gate is satisfied today; it stays because the constants are the
		// contract, and a future edit that un-pins one must turn the feature off
		// rather than read a guessed offset.
		if (!NCT::LinuxLayout::Known(NCT::LinuxLayout::kUnkObject_currentPlayer) ||
			!NCT::LinuxLayout::Known(NCT::LinuxLayout::kUnkObject_currentCameraObject)) {
			static std::once_flag warned;
			WarnOnce(warned,
				"Hook_UpdateCamera: RE::UnkObject's Linux field offsets are not pinned "
				"(kUnkObject_currentPlayer / kUnkObject_currentCameraObject == UNKNOWN) — "
				"current-player/camera tracking is OFF (camera settings still applied)");
			if (_UpdateCamera) {
				_UpdateCamera(a1, a2, a3, a4);
			}
			return;
		}

		const auto  cameraTweaks = CameraTweaks::GetSingleton();
		const auto  a4Base = reinterpret_cast<uintptr_t>(a4);
		cameraTweaks->SetCurrentPlayer(*reinterpret_cast<RE::Player**>(a4Base + NCT::LinuxLayout::kUnkObject_currentPlayer));
		cameraTweaks->SetCurrentCamera(*reinterpret_cast<RE::CameraObject**>(a4Base + NCT::LinuxLayout::kUnkObject_currentCameraObject));

		_UpdateCamera(a1, a2, a3, a4);
#else
		CameraTweaks::GetSingleton()->SetCameraSettings();

		const auto cameraTweaks = CameraTweaks::GetSingleton();
		cameraTweaks->SetCurrentPlayer(a4->currentPlayer);
		cameraTweaks->SetCurrentCamera(a4->currentCameraObject);

		_UpdateCamera(a1, a2, a3, a4);
#endif
	}

	void* Hooks::Hook_HandleCameraInput(uint64_t a1, uint64_t a2, RE::UnkObject* a3, uintptr_t a4)
    {
#if !defined(_WIN32)
		// NEVER INSTALLED ON LINUX. Windows 0x141e10190 is inlined into host
		// 0x2562df0 (tracker 0x141e10190-camera-input-shared, verified-adversarial):
		// there is no call site to rewrite, `_HandleCameraInput` is permanently null,
		// and HookLinux() never installs this function. The Linux behaviour lives in
		// the cave callback NCT_Cave_HandleCameraInput above, which works on the a4
		// pointer directly.
		//
		// The Windows body is therefore compiled ONLY on Windows now. Keeping a Linux
		// twin of it was actively harmful rather than merely dead: it read
		// `a3->currentCameraObject2`, and on Linux that field (RE::UnkObject +0x60,
		// tracker 0x141e0f120 row 9, upgraded to dual-confirmed 2026-09-03 by the
		// witness `cmp BYTE PTR [r12+0x8],0x0`@0x2c7fe0c) is NOT an RE::CameraObject
		// at all — stride 0x1c, byte flag at +0x8, nothing like CameraObject's 0x258
		// layout — while the lines below WRITE floats through it. No Linux code path
		// may cast that field, so the cast is deleted rather than merely gated.
		static std::once_flag warned;
		WarnOnce(warned,
			"Hook_HandleCameraInput was called on Linux, which should be impossible "
			"(no call site exists for win 0x141e10190) — doing nothing");
		(void)a1;
		(void)a2;
		(void)a3;
		(void)a4;
		return nullptr;
#else
		// WINDOWS ONLY. Everything from here to the matching #endif compiles only
		// under _WIN32 (see the #if above). Do not add a Linux arm inside it: the
		// preprocessor has already selected the Linux branch, so such an arm would
		// be excluded on BOTH platforms and never even type-checked. The Linux
		// controller zoom/pitch path is NCT_Cave_HandleCameraInput, above.
		constexpr std::ptrdiff_t kInputIdOffset = 0x00;      // RAWOFFSET
		constexpr std::ptrdiff_t kInputValueAOffset = 0x18;  // RAWOFFSET
		constexpr std::ptrdiff_t kInputValueBOffset = 0x14;  // RAWOFFSET
		const auto               GetCameraObject2 = [](RE::UnkObject* a_obj) { return a_obj->currentCameraObject2; };

		const Offsets::InputID inputId = *reinterpret_cast<Offsets::InputID*>(a4 + kInputIdOffset);
		const bool bIsInControllerMode = *Offsets::bIsInControllerMode;

		auto* settings = Settings::Main::GetSingleton();

		switch (inputId) {
		case Offsets::InputID::kZoomIn:
		case Offsets::InputID::kZoomOut:  // zoom in and out
			{
				ReadLocker locker(settings->Lock);

				const auto cameraObject = GetCameraObject2(a3);
				if (bIsInControllerMode) {
					const auto playerId = Utils::GetPlayerID(a3);
					float* pInputValue = reinterpret_cast<float*>(a4 + kInputValueAOffset);

					const auto cameraTweaks = CameraTweaks::GetSingleton();

					bool bDoZoom;
					bool bCanAdjustPitch = cameraTweaks->CanAdjustPitch(cameraObject);

					if (!bCanAdjustPitch) {
						bDoZoom = true;
					} else if (*settings->UseRightStickPressForZoom) {
						bDoZoom = Offsets::ShouldShowSneakCones(*Offsets::UnkSingletonPtr, playerId);
					} else {
						const auto playerController = Offsets::GetPlayerController(*Offsets::UnkPlayerSingletonPtr, playerId);
						auto toggleInputId = Offsets::InputID::kToggleInputMode;  // (ToggleInputMode - by default left stick click)
						RE::InputValue inputValue;
						Offsets::GetInputValue(*Offsets::UnkInputSingletonPtr, inputValue, toggleInputId, playerController);
						bDoZoom = inputValue.bIsPressed;
					}

					if (bCanAdjustPitch && *settings->SwapZoomAndPitch) {
					    bDoZoom = !bDoZoom;
					}

					const bool bShouldSkipToggleInputMode = *settings->SwapZoomAndPitch ? !bDoZoom : bDoZoom;
					if (bCanAdjustPitch && bShouldSkipToggleInputMode && !*settings->UseRightStickPressForZoom && *pInputValue > CameraTweaks::VANILLA_DEADZONE) {
						// if we're actually zooming, and doing this with the ToggleInputMode held, skip the next ToggleInputMode press
						cameraTweaks->SetSkipToggleInputMode(playerId, true);
					}

					if (bDoZoom) {
						// let it do the original function (zoom)
						cameraTweaks->SetControllerPitchDelta(playerId, 0.f);  // clear the delta
					} else {
						// do pitch instead of zoom
						cameraObject->zoomDelta = 0.f;  // set zoom delta to 0 in case we were just zooming with the controller and then stopped pressing the stick

						const float sign = inputId == Offsets::InputID::kZoomIn ? -1.f : 1.f;

						cameraTweaks->SetControllerPitchDelta(playerId, *pInputValue * sign);

						*pInputValue = 0.f;                         // set input value to 0 so that the game doesn't do anything with it
						return _HandleCameraInput(a1, a2, a3, a4);  // call original to preserve hooking compatibility
					}
				}

				// slower zoom
				auto ret = _HandleCameraInput(a1, a2, a3, a4);
				cameraObject->zoomDelta *= bIsInControllerMode ? *settings->ControllerZoomMult : *settings->MouseZoomMult;
				return ret;
			}
		case Offsets::InputID::kRotateLeft:
		case Offsets::InputID::kRotateRight:  // rotate left and right
	        {
				float* pInputValue = reinterpret_cast<float*>(a4 + kInputValueAOffset);
				if (bIsInControllerMode) {
					// adjust deadzone + add mult from settings
					*pInputValue = CameraTweaks::GetSingleton()->AdjustInputValueForDeadzone(*pInputValue);
				} else {
					// add mult from settings for keyboard rotation
					const auto cameraObject = GetCameraObject2(a3);
					auto ret = _HandleCameraInput(a1, a2, a3, a4);
					ReadLocker locker(settings->Lock);
					cameraObject->currentAngleDelta *= *settings->KeyboardCameraRotationMult;  // apply mult
					return ret;
				}
			    break;
	        }
		case Offsets::InputID::kMouseRotateLeft:
		case Offsets::InputID::kMouseRotateRight:  // mouse rotate left and right
		    {
				ReadLocker locker(settings->Lock);
				float* pInputValue = reinterpret_cast<float*>(a4 + kInputValueBOffset);
				*pInputValue *= *settings->MouseCameraRotationMult;
			    break;
		    }
		}

		// call original
		return _HandleCameraInput(a1, a2, a3, a4);
#endif
    }

#if defined(_WIN32)
    float Hooks::Hook_CalculateCameraPitch(RE::CameraObject* a_cameraObject, uint8_t a2, uint8_t a3)
	{
		float pitch = _CalculateCameraPitch(a_cameraObject, a2, a3);
#else
	// tracker 0x141c719e0-calculate-camera-pitch: the third parameter is
	// dead-arg-eliminated by LLVM (3 params -> 2). Passing a third argument here
	// would put a value in rdx that the callee never reads — harmless — but
	// DECLARING three would make the trampoline's prototype disagree with the
	// callee's, so the arity is fixed here instead.
	float Hooks::Hook_CalculateCameraPitch(RE::CameraObject* a_cameraObject, uint8_t a2)
	{
		float pitch = _CalculateCameraPitch(a_cameraObject, a2);
#endif

		auto cameraTweaks = CameraTweaks::GetSingleton();
		auto playerId = cameraTweaks->GetPlayerIdFromCameraObject(a_cameraObject);
		if (playerId == 0) {  // should never happen
		    return pitch;
		}

		cameraTweaks->CalculateCameraPitch(playerId, a_cameraObject, pitch);

		return pitch;
	}

    void Hooks::Hook_UpdateCameraPitch(uint64_t a1, uint64_t a2, RE::CameraObject* a_cameraObject, uint64_t a4)
	{
#if !defined(_WIN32)
		// NEVER INSTALLED ON LINUX: win 0x141e1ab70 is inlined into UpdateCamera
		// 0x2c7d3a0 (tracker 0x141e1ab70-update-camera-pitch), so there is no call
		// site and `_UpdateCameraPitch` is permanently null — calling the original
		// below would be a null indirect call. The Linux equivalent is the
		// entry/exit cave PAIR (NCT_Cave_UpdateCameraPitch_Entry / _Exit above),
		// which is what makes the raise/restore wrap possible without a callee.
		if (!_UpdateCameraPitch) {
			static std::once_flag warned;
			WarnOnce(warned,
				"Hook_UpdateCameraPitch was called on Linux, which should be impossible "
				"(no call site exists for win 0x141e1ab70) — doing nothing");
			return;
		}
#endif
#if defined(_WIN32)
		constexpr std::ptrdiff_t kDeltaTimeOffset = 0x8;  // RAWOFFSET
#else
		// Same offset on Linux, but pinned rather than assumed: tracker
		// 0x141e0f120 §3.3 matches the read instruction-for-instruction on both
		// sides (`subss xmm0,[rbx+0x8]`@0x141e0f3fa ↔ `subss xmm1,[rax+0x8]`
		// @0x2c7d523) and states "needs no offset change".
		constexpr std::ptrdiff_t kDeltaTimeOffset = NCT::LinuxLayout::kUpdateCameraPitch_deltaTime;
		static_assert(NCT::LinuxLayout::Known(kDeltaTimeOffset),
			"Hook_UpdateCameraPitch reads a4 unconditionally; it has no fail-soft path for an unpinned offset");
#endif
		const float deltaTime = *reinterpret_cast<float*>(a4 + kDeltaTimeOffset);
		auto cameraTweaks = CameraTweaks::GetSingleton();
		cameraTweaks->SetDeltaTime(deltaTime);

		auto currentPlayer = cameraTweaks->GetCurrentPlayer();
		const auto playerId = currentPlayer ? currentPlayer->playerId_38 : 1;

		if (cameraTweaks->IsCameraUnlocked(playerId, a_cameraObject)) {
#if defined(_WIN32)
			const auto cameraDefinition = Offsets::GetCurrentCameraDefinition(a_cameraObject);
#else
			// Inlined at every Linux call site — use the mod's reimplementation.
			const auto cameraDefinition = Utils::GetCurrentCameraDefinition(a_cameraObject->cameraModeFlags);
			if (!cameraDefinition) {
				_UpdateCameraPitch(a1, a2, a_cameraObject, a4);
				return;
			}
#endif
			const float originalPitchAdjustSpeedA = cameraDefinition->pitchAdjustSpeedA_48;
			const float originalPitchAdjustSpeedB = cameraDefinition->pitchAdjustSpeedB_F0;
			const float originalPitchAdjustSpeedC = cameraDefinition->pitchAdjustSpeedC_F4;

			cameraDefinition->pitchAdjustSpeedA_48 = 100000.f;
			cameraDefinition->pitchAdjustSpeedB_F0 = 100000.f;
			cameraDefinition->pitchAdjustSpeedC_F4 = 100000.f;

			cameraTweaks->SetCameraObjectForPlayer(playerId, a_cameraObject);
			_UpdateCameraPitch(a1, a2, a_cameraObject, a4);

			cameraDefinition->pitchAdjustSpeedA_48 = originalPitchAdjustSpeedA;
			cameraDefinition->pitchAdjustSpeedB_F0 = originalPitchAdjustSpeedB;
			cameraDefinition->pitchAdjustSpeedC_F4 = originalPitchAdjustSpeedC;
		} else {
			_UpdateCameraPitch(a1, a2, a_cameraObject, a4);
		}
	}

	void Hooks::Hook_AfterUpdateCameraZoom(uint64_t a1, uint64_t a2, RE::UnkObject* a3, uint64_t a4)
	{
#if !defined(_WIN32)
		// NEVER INSTALLED ON LINUX: win 0x141e1b5a0 is inlined into UpdateCamera
		// 0x2c7d3a0 with a SCATTERED body and no contiguous guest entry (tracker
		// 0x141e1b5a0-after-update-camera-zoom), so the Linux hook is a cave at the
		// guest EXIT 0x2c7fe41 — NCT_Cave_AfterUpdateCameraZoom above.
		if (!_AfterUpdateCameraZoom) {
			static std::once_flag warned;
			WarnOnce(warned,
				"Hook_AfterUpdateCameraZoom was called on Linux, which should be impossible "
				"(no call site exists for win 0x141e1b5a0) — doing nothing");
			return;
		}
#endif
		_AfterUpdateCameraZoom(a1, a2, a3, a4);

		const auto settings = Settings::Main::GetSingleton();
		ReadLocker locker(settings->Lock);
		if (*settings->UnlockedPitchLimitClipping) {
			const auto cameraTweaks = CameraTweaks::GetSingleton();
#if !defined(_WIN32)
			// GetCurrentCamera() is fed by Hook_UpdateCamera. That now runs for real
			// (kUnkObject_currentCameraObject is pinned), so this is an ordinary
			// ordering guard — the zoom hook can fire before the first UpdateCamera,
			// or while UpdateCamera's own hook failed to install — not the
			// permanently-taken path it was when the offsets were unknown.
			if (!cameraTweaks->GetCurrentCamera()) {
				static std::once_flag warned;
				WarnOnce(warned, "AfterUpdateCameraZoom: no current camera yet (Hook_UpdateCamera has not supplied one) — under-floor zoom clipping skipped");
				return;
			}
#endif
			CameraTweaks::GetSingleton()->AdjustCameraZoomForPitch(a1, a2, cameraTweaks->GetCurrentCamera());
		}
	}

    int16_t* Hooks::Hook_HandleToggleInputMode(uint64_t a1, int16_t& a_outResult, Offsets::InputID* a_inputId)
	{
#if !defined(_WIN32)
		// NEVER INSTALLED ON LINUX: win 0x141c4bd80 is inlined into host 0x3b905e0
		// (tracker 0x141c4bd80-handle-toggle-input-mode). The Linux equivalent is the
		// rel32 retarget at 0x3b90780 plus NCT_ShouldBlockToggle above — which is
		// this function's body minus the call to the original.
		if (!_HandleToggleInputMode) {
			static std::once_flag warned;
			WarnOnce(warned,
				"Hook_HandleToggleInputMode was called on Linux, which should be impossible "
				"(no call site exists for win 0x141c4bd80) — doing nothing");
			return nullptr;
		}
#endif
		if (*a_inputId == Offsets::InputID::kToggleInputMode) {  // left stick click "ToggleInputMode"
			const auto cameraTweaks = CameraTweaks::GetSingleton();
#if defined(_WIN32)
			const auto playerId = *reinterpret_cast<int16_t*>(a1 + 0x168);  // RAWOFFSET
#else
			// Same Known() guard the cave twin performs — the constant is pinned
			// today, but nothing else here would stop a read at offset -1 if a
			// later edit un-pinned it.
			if (!NCT::LinuxLayout::Known(NCT::LinuxLayout::kHandleToggleInputMode_playerId)) {
				static std::once_flag warned;
				WarnOnce(warned,
					"Hook_HandleToggleInputMode: kHandleToggleInputMode_playerId is not pinned — "
					"toggle suppression is OFF");
				return _HandleToggleInputMode(a1, a_outResult, a_inputId);
			}
			const auto playerId = *reinterpret_cast<int16_t*>(a1 + NCT::LinuxLayout::kHandleToggleInputMode_playerId);
#endif
			if (cameraTweaks->ShouldSkipToggleInputMode(playerId)) {
				cameraTweaks->SetSkipToggleInputMode(playerId, false);
				a_outResult = 0;
				return &a_outResult;  // do not call original - block input
			}
		}

		return _HandleToggleInputMode(a1, a_outResult, a_inputId);
	}

    void Hooks::Hook_SetDefaultZoom(RE::CameraObject* a_cameraObject)
	{
#if !defined(_WIN32)
		// tracker 0x141feb960-set-default-zoom: the game writes the zoom pair through
		// an interpolation sub-object whose pointer moved from a_cameraObject+0x20
		// (Windows) to +0x0 (Linux). The float offsets inside it (+0x5c desiredZoom,
		// +0x160 currentZoom_160) are byte-identical on both sides, so the
		// RE::CameraObject field names apply unchanged to the sub-object.
		auto* zoomTarget = *reinterpret_cast<RE::CameraObject**>(
			reinterpret_cast<uintptr_t>(a_cameraObject) + NCT::LinuxLayout::kCameraObject_interpSubObject);
		if (!zoomTarget) {
			_SetDefaultZoom(a_cameraObject);
			return;
		}

		const float desiredZoom = zoomTarget->desiredZoom;

		_SetDefaultZoom(a_cameraObject);

		const auto settings = Settings::Main::GetSingleton();
		ReadLocker locker(settings->Lock);
		if (!*settings->ResetZoomOnZoneChange) {
			zoomTarget->desiredZoom = desiredZoom;
			zoomTarget->currentZoom_160 = desiredZoom;
		}
#else
		float desiredZoom = a_cameraObject->desiredZoom;

		_SetDefaultZoom(a_cameraObject);

		const auto settings = Settings::Main::GetSingleton();
		ReadLocker locker(settings->Lock);
		if (!*settings->ResetZoomOnZoneChange) {
			a_cameraObject->desiredZoom = desiredZoom;
			a_cameraObject->currentZoom_160 = desiredZoom;
		}
#endif
	}

    bool Hooks::Hook_SDLMouseYHook(uint64_t a1, uint64_t a2, bool a3, int a_deltaY)
	{
		// Windows only — on Linux src/Linux/SdlInput.cpp writes delta_y directly and
		// this hook is never installed (see HookLinux()).
		CameraTweaks::GetSingleton()->delta_y = a_deltaY;

		return _SDLMouseYHook(a1, a2, a3, a_deltaY);
	}
}
