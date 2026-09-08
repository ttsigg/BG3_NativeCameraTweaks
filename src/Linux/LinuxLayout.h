#pragma once

// LinuxLayout.h — the SINGLE place every Linux-only magic number in this mod lives.
//
// Rule for this file: no number goes in without (a) the tracker key it came from
// (`docs/linux-port/tracking/functions/<key>/STATUS.md`) and (b) the date of the
// adversarial review that pinned it. A number nobody has pinned is spelled
// `kUnknown` and the call site that needs it must fail soft (WARN once, feature
// off) rather than read a guessed offset out of the game's memory.
//
// The Windows values are kept in the comments only. Windows compilation never
// sees this header (the whole body is `#if !defined(_WIN32)`), so nothing here
// can change the MSVC build.
//
// Provenance vocabulary used below (see `.claude/skills/tracking-re-result-provenance`):
//   verified-adversarial : worker claim + refute-biased reviewer, both re-derived
//                          from the binaries. Safe to dereference.
//   observed             : read out of a disassembly in-session but never put
//                          through the review protocol. Usable only where a wrong
//                          value cannot corrupt game memory.
//   UNKNOWN              : nobody has pinned it. Gate the feature off.

#if !defined(_WIN32)

#	include <cstddef>
#	include <cstdint>

namespace NCT::LinuxLayout
{
	// Sentinel for "the RE pipeline has not pinned this yet".
	inline constexpr std::ptrdiff_t kUnknown = -1;

	[[nodiscard]] inline constexpr bool Known(std::ptrdiff_t a_value) noexcept
	{
		return a_value != kUnknown;
	}

	// ---------------------------------------------------------------------
	// ls::GlobalSwitches — the object `Offsets::UnkCameraSingletonPtr` points at
	//
	// tracker: 0x141c714e0-get-current-camera-definition
	// review : opus-4.8 adversarial, 2026-09-01 — CONFIRMED. Every one of the four
	//          was re-read off the CONFIRMED CalculateCameraPitch Linux body
	//          (0x2c85740, selector at 0x2c858f4-0x2c8596e), NOT inferred from a
	//          uniform delta; that is why the bool moves +0x30 while the three
	//          definition pointers move +0x28 and the mismatch is benign.
	// tier   : verified-adversarial
	// ---------------------------------------------------------------------
	inline constexpr std::uint32_t kGlobalSwitches_cameraBoolOffset = 0x1362;  // Windows 0x1332
	inline constexpr std::uint32_t kGlobalSwitches_unkCameraOffset = 0xC80;    // Windows 0xC58
	inline constexpr std::uint32_t kGlobalSwitches_explorationCameraOffset = 0x7C4;  // Windows 0x79C
	inline constexpr std::uint32_t kGlobalSwitches_combatCameraOffset = 0x958;      // Windows 0x930

	// ---------------------------------------------------------------------
	// RE::CameraDefinition — the object those three offsets point AT
	//
	// Deliberately NOT re-declared as kCameraDefinition_* constants: the field
	// offsets are NOT Linux-only magic numbers. RE/Camera.h declares the struct as
	// a dense run of `float` members with no holes, and every one of its offsets is
	// pinned at COMPILE time by RE/Generated/LayoutManifest.inl (`sizeof(...) ==
	// 400` plus one `offsetof` static_assert per field), compiled into the shipped
	// .so by cmake/linux/CMakeLists.txt's src/RE/*.cpp glob. Duplicating them here
	// would create a second copy that can silently disagree with the header the
	// writes actually go through — the opposite of what this file is for.
	//
	// What DOES need pinning on Linux is that the game's definition object has the
	// same shape as that header, and it does:
	//
	//   * STRIDE. combat - exploration is 0x958 - 0x7C4 == 0x194 on Linux and
	//     0x930 - 0x79C == 0x194 on Windows. Identical spacing means the same
	//     struct version, not merely a same-named one.
	//   * WITNESSES. 27 distinct field offsets were read straight off the Linux
	//     disassembly at a definition base (ls::GlobalSwitches + 0x7C4 / +0x958 /
	//     +0xC80), and every one lands on the SAME offset as the Windows header
	//     (`bg3re disasm`, 2026-09-03):
	//       +0x28 `add rsi,0x28`@0x2c85848        +0x2C `add r8,0x2c`@0x2c85803
	//       +0x30 `add rsi,0x30`@0x2c85943        +0x34 `add r8,0x34`@0x2c8593a
	//       +0x38 `movss xmm1,[rax+0x38]`@0x2c7e9f8
	//       +0x50 `subss xmm2,[rdx+0x50]`@0x2c80377
	//       +0x54 `movss xmm0,[rax+0x54]`@0x39e3482
	//       +0x7C `add rsi,0x7c`@0x2c81092
	//       +0xC4 `movss xmm0,[rcx+0xc4]`@0x2c83c8d
	//       +0xC8 `add r8,0xc8`@0x2c858db          +0xCC `movss xmm4,[rax+0xcc]`@0x2c7fd4f
	//       +0xD4 `movss xmm8,[rax+0xd4]`@0x2c81618
	//       +0xD8 `minss xmm8,[rax+0xd8]`@0x2c81621
	//       +0x104 @0x66149f0                      +0x108 @0x2c7f1c8
	//       +0x120 @0x2c7f1b3                      +0x12C @0x2c806b6
	//       +0x130 @0x2c806be                      +0x134 @0x2c7ed22
	//       +0x140 @0x2c814ed                      +0x144 @0x2c8150c
	//       +0x160 @0x2c85904                      +0x164 @0x2c857bc
	//       +0x168 `add rdx,0x168`@0x2c858bb       +0x16C @0x2c858b3
	//       +0x170 `add rdx,0x170`@0x2c858cf       +0x174 @0x2c858c7
	//       +0x178 `add rdx,0x178`@0x2c85954       +0x17C @0x2c8594c
	//
	// Those witnesses bracket the two groups nobody has a per-offset Linux witness
	// for, tightly and on both sides:
	//   * offset multipliers +0x64/+0x68  — between confirmed +0x54 and +0x7C
	//   * FOV +0x84/+0x88/+0x8C/+0x90 and tactical FOV +0xD0
	//                                     — between confirmed +0x7C and +0xC4/+0xD4
	// For a hole-free float run of known stride, a field that moved inside such a
	// bracket would have had to move its confirmed neighbours too. So
	// CameraTweaks::SetCameraSettings' FOV and offset-multiplier writes are NOT
	// gated: they carry the same evidence class as the pitch and zoom writes beside
	// them, which the tracker rows below already pin directly.
	//
	// review : review-batch 2026-09-03 (layout-gates lens) raised these two groups
	//          as unwitnessed; re-derived above from the binary rather than assumed.
	// tier   : observed (stride + 27 same-offset witnesses); the C++ offsets
	//          themselves are compile-time-asserted, so a header edit cannot drift
	//          them silently.
	// ---------------------------------------------------------------------
	// Stride between the exploration and combat definitions. Asserted against the
	// two offsets above so a future edit to either cannot break the pairing.
	inline constexpr std::uint32_t kCameraDefinition_stride = 0x194;
	static_assert(kGlobalSwitches_combatCameraOffset - kGlobalSwitches_explorationCameraOffset ==
			kCameraDefinition_stride,
		"exploration/combat camera definition spacing no longer matches the measured Linux stride");

	// ---------------------------------------------------------------------
	// RE::UnkObject — Hook_UpdateCamera's 4th argument
	//
	// tracker: 0x141e0f120-update-camera
	// review : pilot adversarial, 2026-09-01 — CONFIRMED as a function counterpart,
	//          and it records "by-value a4 struct is member-reordered". The
	//          reordering was mapped slot-by-slot in that STATUS.md §3.2 (all 16
	//          qword slots, updated 2026-09-03) and its §3.5.
	// tier   : per field, NOT per struct — §3.2 grades each row separately:
	//            row 3  currentPlayer        Win +0x10 -> Lin +0x20  DUAL-CONFIRMED
	//            row 7  currentCameraObject  Win +0x30 -> Lin +0x00  DUAL-CONFIRMED
	//            row 9  currentCameraObject2 Win +0x40 -> Lin +0x60  builder-only (weak)
	//
	// Rows 3 and 7 each carry a *consumer* witness on both sides, which is what
	// makes them safe to dereference:
	//   row 3: Win `mov rcx,[rsi+0x10]`@0x141e0f1bd -> `movsx r8,WORD[rcx+0x38]`
	//          @0x141e0f1d5   ↔   Lin `mov rax,[r14+0x20]`@0x2c7d456 ->
	//          `movsx edx,WORD[rax+0x38]`@0x2c7d493   (the playerId_38 read)
	//   row 7: Win `mov rdi,[rsi+0x30]`@0x141e0f1b9 -> `cmp BYTE[rdi+0x169],0`
	//          @0x141e0f20d   ↔   Lin `mov r15,[r14]`@0x2c7d402 ->
	//          `cmp BYTE[r15+0x169],0`@0x2c7d42a
	// The reviewer's own words: "Rows 3, 7, 8, 16 are dual-confirmed and I would
	// hook against them today."
	//
	// Row 9 (currentCameraObject2) was UPGRADED to dual-confirmed by the
	// 0x141e0f120 review (2026-09-03): the consumer witness §3.5 said could not be
	// obtained from the *callee* pair does exist in the HOST — `cmp BYTE PTR
	// [r12+0x8],0x0`@`0x2c7fe0c`, two instructions before the AfterUpdateCameraZoom
	// guest exit (re-read in-session: `bg3re disasm 0x2c7fe41 --around 12`). So the
	// offset is pinned at 0x60.
	//
	// ⚠ IT IS STILL NOT AN `RE::CameraObject*`. The same review records stride 0x1c
	// and a byte flag at +0x8 — nothing like CameraObject's 0x258 layout. The
	// Windows hook body treats it as one (`cameraObject->zoomDelta`,
	// `->currentAngleDelta`), which is upstream's business on upstream's build;
	// **no Linux code path may cast this field to RE::CameraObject*.** The Linux
	// HandleCameraInput cave takes its camera from `CameraTweaks::GetCurrentCamera()`
	// (fed by Hook_UpdateCamera through row 7) instead — see Hooks.cpp
	// `NCT_Cave_HandleCameraInput`. The constant is pinned here so the *offset* is
	// on record with its evidence, not so that a cast becomes legal.
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kUnkObject_currentPlayer = 0x20;          // Windows 0x10
	inline constexpr std::ptrdiff_t kUnkObject_currentCameraObject = 0x00;    // Windows 0x30
	inline constexpr std::ptrdiff_t kUnkObject_currentCameraObject2 = 0x60;   // Windows 0x40 — NOT a CameraObject*

	// ---------------------------------------------------------------------
	// HandleCameraInput's a4 (the raw "input event" block the mod pokes)
	//
	// tracker: 0x141e10190-camera-input-shared
	// review : two adversarial passes 2026-09-03; the "Dispatcher addendum
	//          (2026-09-03) — adopting the second review's amendments" adopts
	//          **layout delta ZERO** for a4 as the port contract: the fields
	//          +0x0/+0x6/+0x7/+0x14/+0x18/+0x1c are read at the same offsets on
	//          both sides. Two of them are exact-offset matches taken from the
	//          Linux side's own reloads of the a4 slot (STATUS.md §"a4 provenance"):
	//            0x2563be5 `mov rax,[rsp+0x18]` -> `cmp BYTE[rax+0x1c],0`   (a4+0x1c)
	//            0x2563bfd `mov rax,[rsp+0x18]` -> `cmp BYTE[rax+0x7],8`    (a4+0x7)
	//            0x25631f8 `mov rax,[rsp+0x18]` -> `mov edx,[rax]`,
	//                                              `movss xmm2,[rax+0x14]`  (a4+0x0, +0x14)
	// tier   : verified-adversarial (delta zero, whole-struct)
	//
	// The Linux consumer of these three is the cave callback
	// `NCT_Cave_HandleCameraInput` (Hooks.cpp), which reads a4+0x0 (the InputID)
	// and read/writes the two floats through exactly these constants.
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kHandleCameraInput_inputId = 0x00;      // Windows a4+0x00
	inline constexpr std::ptrdiff_t kHandleCameraInput_inputValueA = 0x18;  // Windows a4+0x18
	inline constexpr std::ptrdiff_t kHandleCameraInput_inputValueB = 0x14;  // Windows a4+0x14

	// ---------------------------------------------------------------------
	// UpdateCameraPitch's a4 -> deltaTime
	//
	// tracker: 0x141e0f120-update-camera §3.3 ("deltaTime for Hook_UpdateCameraPitch
	//          — corrected"), re-derived 2026-09-03.
	// review : that section corrects an earlier attempt and pins a4 as UpdateCamera's
	//          own a3 (Win home slot [rbp+0x460] / Lin [rsp+0x78]), with the float
	//          read matched instruction-for-instruction on both sides:
	//            Win `subss xmm0,DWORD PTR [rbx+0x8]`@0x141e0f3fa / @0x141e0f415
	//            Lin `subss xmm1,DWORD PTR [rax+0x8]`@0x2c7d523  / @0x2c8049c
	//          Its stated port consequence: "Hooks.cpp:542 ... needs no offset
	//          change". Note the same context struct carries a second float at +0xC.
	// tier   : dual-confirmed (read-only; a wrong value here scales an animation,
	//          it cannot corrupt game memory).
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kUpdateCameraPitch_deltaTime = 0x8;  // Windows a4+0x8

	// ---------------------------------------------------------------------
	// HandleToggleInputMode's a1 -> playerId
	//
	// tracker: 0x141c4bd80-handle-toggle-input-mode (guest) inside
	//          0x1405ea2b0-call-specific-command (host 0x3b905e0)
	// review : opus adversarial, 2026-09-01 — CONFIRMED the guest body at
	//          0x3b906b0. Read in-session (2026-09-03) at 0x3b906c5:
	//          `movsx rcx,WORD PTR [r15+0x168]`, and at 0x3b90766
	//          `add r15,0x168` — same +0x168 the Windows hook uses.
	// tier   : observed (matches Windows; only read, never written, and the hook
	//          that would use it is disabled anyway).
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kHandleToggleInputMode_playerId = 0x168;  // Windows a1+0x168

	// ---------------------------------------------------------------------
	// RE::CameraObject — SetDefaultZoom's interpolation sub-object
	//
	// tracker: 0x141feb960-set-default-zoom
	// review : opus-4-8 adversarial, 2026-09-01 — CONFIRMED. Re-derived here on
	//          2026-09-03: Windows 0x141feb9b3 `mov rdi,[rsi+0x20]` then the twin
	//          write `movss [rdi+0x5c] / [rdi+0x160]` at 0x141febb3f; Linux
	//          0x6602bc1 `mov rbx,[r14]` (i.e. +0x0) then the same twin write at
	//          0x6602c8f. The two float offsets (+0x5c desiredZoom,
	//          +0x160 currentZoom_160) are byte-identical on both sides.
	// tier   : verified-adversarial
	//
	// NOTE: upstream's Windows hook body reads `a_cameraObject->desiredZoom`
	// directly, i.e. a_cameraObject+0x5c, which is NOT the object the game writes
	// ([a_cameraObject+0x20]+0x5c). That is a pre-existing upstream bug; the
	// Windows path is left exactly as it was, and only the Linux path goes through
	// the sub-object pointer.
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kCameraObject_interpSubObject = 0x0;  // Windows +0x20

	// ---------------------------------------------------------------------
	// GetPlayerController's hash-bucket map (clang inlined the callee)
	//
	// tracker: 0x142067ba0-player-controller-fns
	// review : opus adversarial, 2026-09-01 — CONFIRMED, including the
	//          proof-of-presence that Windows 0x143baaeb0's 9-instruction body
	//          reappears verbatim at Linux 0x25c6df8-0x25c6e32.
	//          Loop re-derived in-session 2026-09-03 from
	//          `bg3re disasm 0x25c6df8`:
	//            rdi   = *(void**)<map global>
	//            r8d   = *(u32*)(rdi+0x34)          ; bucketCount, 0 -> absent
	//            rdx   = (key % bucketCount) * 8 + *(uptr*)(rdi+0x38)
	//            node  = *(uptr*)rdx                ; 0 -> absent
	//            loop  : if (*(u16*)(node+8) != key) { node = *(uptr*)node; ... }
	//            value = *(u8*)(node+0x10)          ; 0xff -> absent
	//          Every one of +0x34/+0x38/+0x8/+0x10 is identical to the Windows
	//          function read directly at 0x143baaeb0 (re-read in-session).
	// tier   : verified-adversarial
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kPlayerControllerMap_bucketCount = 0x34;
	inline constexpr std::ptrdiff_t kPlayerControllerMap_buckets = 0x38;
	inline constexpr std::ptrdiff_t kPlayerControllerNode_next = 0x0;
	inline constexpr std::ptrdiff_t kPlayerControllerNode_key = 0x8;
	inline constexpr std::ptrdiff_t kPlayerControllerNode_value = 0x10;
	inline constexpr std::uint8_t   kPlayerControllerAbsent = 0xFF;

	// ---------------------------------------------------------------------
	// ShouldShowSneakCones' FIRST bucket table (the `NCT:UnkSingletonPtr` global,
	// Win 0x146291718 -> Linux 0x7c061e8)
	//
	// tracker: 0x14182acc0-should-show-sneak-cones (verified-adversarial, reviewer
	//          opus-5 nct-re-remainder 2026-09-03 CONFIRMED: "no standalone
	//          counterpart anywhere in .text", so the mod must reimplement it) and
	//          0x141b48c40-unk-singleton-ptr for the global itself.
	// review : re-derived instruction-by-instruction in-session 2026-09-03 from
	//          `bg3re disasm 0x2d1daf0 --around 45` (Linux, inlined copy inside
	//          host 0x2d1dad0) against `bg3re disasm 0x14182acc0 --around 45`
	//          (Windows, the standalone function). Byte-identical shapes:
	//            count   Lin `mov r8d,[rdi+0x24]`   @0x2d1daf0
	//                    Win `mov eax,[rcx+0x24]`   @0x14182acc4
	//            buckets Lin `add rdx,[rdi+0x28]`   @0x2d1db11
	//                    Win `mov rax,[rcx+0x28]`   @0x14182acda
	//            next    Lin `mov rdx,[rdx]`        @0x2d1db15
	//                    Win `mov rcx,[rcx]`        @0x14182acf7
	//            key u16 Lin `cmp WORD[rdx+0x8],si` @0x2d1db1d
	//                    Win `cmp r9w,WORD[rcx+0x8]`@0x14182acf0
	// tier   : verified-adversarial
	//
	// ⚠ CORRECTION to the port brief's prose: this first table carries **no value
	// byte**. A hit is a pure presence test — Windows falls straight through to the
	// SECOND table (0x14182ad06) without reading the node, and a miss returns false
	// (`xor al,al; ret`@0x14182acff, mirrored by the Linux `je 0x2d1db75` bail-outs
	// at 0x2d1daff/0x2d1db1b). The sub-id and its 0xFF default come from the SECOND
	// table only (kPlayerControllerMap_* above — the same bucket walk, re-verified
	// against `bg3re disasm 0x25c6df8 --around 14`: 0x2d1db2d/0x2d1db42/0x2d1db46/
	// 0x2d1db4e/0x2d1db54 line up field-for-field with 0x25c6dff/0x25c6e18/
	// 0x25c6e1c/0x25c6e28/0x25c6e2e).
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kSneakConeMap_bucketCount = 0x24;
	inline constexpr std::ptrdiff_t kSneakConeMap_buckets = 0x28;
	inline constexpr std::ptrdiff_t kSneakConeNode_next = 0x0;
	inline constexpr std::ptrdiff_t kSneakConeNode_key = 0x8;

	// The input-action id ShouldShowSneakCones queries. Same literal on both sides:
	// Lin `mov esi,0xdc`@0x2d1db63, Win `mov DWORD PTR [rsp+0x40],0xdc`@0x14182ad53.
	// The tracker's signature field names it: action id 0xdc == "ShowSneakCones"
	// per the Linux input-action registration table 0x4144400.
	inline constexpr std::int32_t kInputAction_ShowSneakCones = 0xDC;

	// ---------------------------------------------------------------------
	// AdjustCameraZoomForPitch's `a1 + 0x118` RAWOFFSET (last GetFloorLevel arg)
	//
	// tracker: 0x141e1b5a0-after-update-camera-zoom
	// review : opus-5 adversarial, Attempt 2, CONFIRMED 2026-09-07. Pinned via the
	//          fan-in-1 Windows callee 0x141e183c0 (UpdateCamera's sole caller),
	//          whose one and only r14 dereference is
	//          `141e184a2: mov rax,[r14+0x118]` (r14 <- rcx @141e183fe, sole def;
	//          rcx = UpdateCamera's own r15 = its arg1, i.e. this row's `a1`).
	//          Linux mirror inlined into the same host (0x2c7d3a0):
	//          `2c7e858: mov r8,[r12+0x110]` (r12 <- [rsp+0x18] = a1 @2c7e82d) ->
	//          `2c7e874: call 2694850` (GetFloorLevelLinux). The reviewer traced
	//          the value through the wrapper on both sides (callee-side proof:
	//          Windows GetFloorLevel reads it at `141febcf8: mov rcx,[rbp+0xf0]`
	//          then `141febd10: mov rbx,[rcx+0x90]`; Linux GetFloorLevelLinux opens
	//          `2694861: mov rax,[r8+0x90]` — the identical +0x90/+0x80 chain — so
	//          Linux r8 IS Windows arg7, independent of argument-position
	//          reasoning) and reproduced the mapping in a second, unrelated
	//          function pair (win 0x141e18ba0 `mov rcx,[rax+0x118]` <-> lin
	//          0x2c918f0 `mov r8,[r14/rcx+0x110]`).
	// tier   : verified-adversarial
	// ---------------------------------------------------------------------
	inline constexpr std::ptrdiff_t kZoomAdjustContext_floorQueryArg = 0x110;  // Windows a1+0x118

	// =====================================================================
	// Cave hooks and in-body patches — targets with NO Linux call site
	//
	// Addresses below are UNBIASED bg3 ELF vaddrs; add DKUtil's module base at
	// runtime. Each site carries its own enable flag plus the exact bytes the
	// installer must find there: Hooks.cpp refuses to patch a site whose bytes do
	// not match (the same self-defence `Catalog::LooksLikeDirectCall` gives the
	// write_call sites), because every address here is valid for exactly one game
	// build and the site catalog's identity gate can only be as good as the
	// `linux_build_id` the generator put in it.
	//
	// PREREQUISITES for flipping any flag — there are FOUR, not one:
	//   1. the missing constant(s) named in that site's note;
	//   2. a sound argument source for the chosen site — the register or host stack
	//      slot holding each callback argument is a property of the SITE, not of
	//      the function, and at least one reviewed site had a path-dependent
	//      register (the retired 0x3b9079f placement below);
	//   3. a rip-relative audit of the stolen bytes. The fork's
	//      `detail::RelocateStolenBytes` (DKUtil Internal/CaveHook.hpp) now decodes
	//      and RE-RELOCATES a rip-relative displacement rather than the naive
	//      memcpy docs/linux-port/CAVE-HOOKS.md §5 documented, and refuses the hook
	//      outright when it cannot prove the range safe — but it can only do that
	//      for the encodings its small decoder models. A window that contains one
	//      is still better handled by re-materialising the access absolutely in the
	//      epilog (which is what kCave_HandleCameraInput does) than by trusting the
	//      replay path;
	//   4. trampoline budget. `kTrampolineSize` must cover it. MEASURED by
	//      assembling this mod's own CaveProlog/CaveEpilog/ToggleGate with the
	//      vendored Xbyak and reading getSize(): prolog 140-150 B depending on how
	//      many arguments it materialises, epilog 133 B (+5 for the pitch-entry
	//      tail, +13 for the absolute-global-load tail), plus ~31 B of
	//      imm64/sub/call/add/stolen-bytes/jmp glue AddCaveHook emits per cave —
	//      so ~320 B per cave, ~1.3 KB for the four. The toggle gate is 61 B and
	//      each live write_call rel hook ~14 B.
	//      DKUtil FATALs — and on Linux Logger's report_error() ends in
	//      std::abort() — when a trampoline write exceeds the reservation, i.e. the
	//      game dies inside dlopen.
	//
	// `*_ArgReg` values are Xbyak::Operand register codes; Hooks.cpp static_asserts
	// each against the Xbyak constant it names, so these stay readable without this
	// header depending on Xbyak. `*_ArgSlot` values are displacements off the HOST
	// rsp at the site — the cave prolog reads them through the original rsp it
	// saved, so callers never have to hand-adjust for the trampoline's pushes.
	// =====================================================================

	// Trampoline reservation for the whole mod (Hooks::Hook on Linux). Sized for
	// FOUR caves (UpdateCameraPitch entry + exit, HandleCameraInput,
	// AfterUpdateCameraZoom) plus the toggle gate — ~1.4 KB measured, see
	// prerequisite 4 above — with room to spare. Upstream's Windows value is
	// 1 << 7 = 128 B and is left untouched on that platform. HookLinux() logs the
	// actual consumed/capacity pair at the end of installation.
	inline constexpr std::size_t kTrampolineSize = 1u << 12;  // 4 KiB = one page

	// =====================================================================
	// (1) UpdateCameraPitch (win 0x141e1ab70) — TWO caves in UpdateCamera 0x2c7d3a0
	//
	// tracker: 0x141e1ab70-update-camera-pitch
	// review : "Adversarial review — Attempt 2 (cave exit) — 2026-09-03", opus-5,
	//          **CONFIRMED**. R1 re-derived every cited instruction; R3 found no
	//          rip-relative operand in either window; R4 enumerated the inbound
	//          branch edges three independent ways; R5 reproduced both patterns and
	//          showed the naive 5-byte forms are NOT unique (200+ hits — there is a
	//          byte-identical `48 89 EF 31 F6` twin at 0x2c82da4 in the same
	//          function reading +0x13c/+0x148/+0x14c instead, so the installer must
	//          use the 12-byte locator, never the bare stolen bytes); R7 swept the
	//          whole 0x83a0-byte host and proved each of the three pitchAdjustSpeed
	//          fields is read exactly once, all three inside [entry, exit).
	// tier   : verified-adversarial
	//
	// The Windows hook is a WRAP — raise pitchAdjustSpeed A/B/C to 100000, call the
	// original, restore. Both halves now have a home, which is what un-blocks it:
	//
	//   ENTRY 0x2c7f902, steal 5 = `48 89 EF 31 F6` (`mov rdi,rbp`@0x2c7f902 +
	//     `xor esi,esi`@0x2c7f905), resume 0x2c7f907 (`call 0x2c85740`
	//     = CalculateCameraPitch, untouched). Two whole instructions, neither
	//     rip-relative.
	//     ⚠ They are NOT replayed through a kRestore* flag. `xor esi,esi` is
	//     opcode 0x31, which is outside `detail::DecodeStolenInsn`'s modelled set
	//     (0x8B/0x8D/0x89/0x3B/0x80/0xC6 + a short 0F list), so
	//     `RelocateStolenBytes` would REFUSE the whole hook — the designed
	//     "never guess" behaviour of CAVE-HOOKS.md §5, not a bug. Instead the
	//     epilog re-materialises both instructions itself, after every pop, as
	//     `mov rdi,rbp; xor esi,esi` — semantically identical (neither reads rip
	//     or rsp, and EFLAGS are dead into the `call` that follows, which is why
	//     the host's own `xor` was free to clobber them there too).
	//     0x2c7f905
	//     is not a branch target anywhere in the host; 0x2c7f902 is one (from
	//     0x2c804c2 and 0x2c84449) but that is the window's own first byte, which
	//     the 5-byte jmp preserves.
	//   EXIT 0x2c7f961, steal 5 = `48 8B 44 24 78` (`mov rax,[rsp+0x78]`), resume
	//     0x2c7f966. One whole instruction, rsp-relative (NOT rip-relative), and
	//     its result is consumed two instructions later by
	//     `mulss xmm4,[rax+0xc]`@0x2c7f973 — so the replay MUST land in rax. It
	//     does: the trampoline replays it after the epilog has fully restored rsp,
	//     so the displacement needs no adjustment and `pop rax` has already run.
	//     Its only inbound edge (`je 2c7f961`@0x2c7f957) lands on the first byte.
	//
	// a_cameraObject = rbp at BOTH windows (callee-saved across the intervening
	// call; never written between 0x2c7f902 and 0x2c7f9a1).
	// deltaTime = *(float*)([rsp+0x78] + kUpdateCameraPitch_deltaTime) — [rsp+0x78]
	// is UpdateCamera's own a3 spill, stored once at `mov [rsp+0x78],rdx`@0x2c7d3bb
	// and never written again in the whole function (22 reads, zero further
	// stores), so it is valid at both windows.
	//
	// CAVEAT carried from the review (C3): the +0x8 field offset itself is Windows-
	// derived; the only Linux-side evidence in this function is the sibling +0xc.
	// A wrong value here scales an animation, it cannot corrupt memory.
	// CAVEAT (C5): if CalculateCameraPitch throws, the exit cave never runs. The
	// Windows wrapper has the same exposure; Hooks.cpp adds a raised-latch so the
	// next entry recovers instead of leaving the speeds at 100000 forever.
	// =====================================================================
	inline constexpr std::uintptr_t kCave_UpdateCameraPitch_HostVA = 0x2c7d3a0;
	inline constexpr std::uintptr_t kCave_UpdateCameraPitch_EntryVA = 0x2c7f902;
	inline constexpr std::size_t    kCave_UpdateCameraPitch_EntryStolen = 5;
	inline constexpr std::uintptr_t kCave_UpdateCameraPitch_ExitVA = 0x2c7f961;
	inline constexpr std::size_t    kCave_UpdateCameraPitch_ExitStolen = 5;
	// rbp == RE::CameraObject* at both windows; Xbyak::Operand::RBP.
	inline constexpr int            kCave_UpdateCameraPitch_ArgReg = 5;
	// Displacement off the HOST rsp of UpdateCamera's a3 spill (the DeltaCtx*).
	inline constexpr std::int32_t   kCave_UpdateCameraPitch_DeltaCtxSlot = 0x78;
	inline constexpr bool           kCave_UpdateCameraPitch_Enabled = true;

	// =====================================================================
	// (2) HandleToggleInputMode (win 0x141c4bd80) — rel32 retarget in host 0x3b905e0
	//
	// tracker: 0x141c4bd80-handle-toggle-input-mode
	// review : "Adversarial review" + "Dispatcher addendum (2026-09-03) — patch
	//          design adopted with the review's one-token fix". The Attempt-2 design
	//          was REJECTED on exactly one token (its block exit targeted 0x3b90a06,
	//          which is the next-handler-is-NULL path); the corrected design was
	//          re-derived and verified, and is what is encoded below.
	// tier   : verified-adversarial
	//
	// This is NOT a cave: nothing is stolen and no instruction is displaced. The
	// site is the 6-byte `je 0x3b90a0a` at 0x3b90780 (`0F 84 84 02 00 00`) and ONLY
	// its rel32 operand is rewritten, to point at a gate stub in the trampoline.
	// The `r14d != 0xc0` fallthrough into 0x3b90786 stays byte-for-byte identical.
	// Locator `4D 89 E6 0F 84 ?? ?? ?? ?? 49 8B 86 88 00 00 00`, 1/1 whole-.text,
	// site at +3.
	//
	// Gate inputs at the site (review §3, traced exhaustively):
	//   playerId  = *(int16*)r15 — `add r15,0x168`@0x3b90766 has already run, so
	//               r15 == a1 + kHandleToggleInputMode_playerId here.
	//   InputID*  = rbx (set at 0x3b905f8, read again at `cmp [rbx+0x1c],0`
	//               @0x3b90a3f, so unmodified across the gate).
	// Register discipline: rsp ≡ 0 (mod 16) throughout this function's body
	// (prologue = 6 pushes + `sub rsp,0x98` = 208 bytes, 208 % 16 == 0), so the gate
	// does `sub rsp,8; push rdi` to keep the call 16-byte aligned. **rdi is the only
	// caller-saved register live across the site** — it is consumed by the very
	// first instruction of the allow target (`mov [rsp+0x8],rdi`@0x3b90a0a);
	// rax/rcx/rdx/rsi/r8 are all overwritten before their next read on both exits,
	// and the whole 0x3b905e0..0x3b90f90 host touches xmm exactly twice
	// (0x3b90aee/0x3b90afe, defining xmm0 from r13), so no XMM is live here.
	// EFLAGS are dead at both exits (each begins with a mov).
	//
	// Exits:
	//   allow  -> 0x3b90a0a, the original je target (run the game's toggle handler)
	//   block  -> 0x3b90786, the guest's own return-0 / forward-to-next-handler
	//             exit (≡ Windows 0x1405ea31e). NOT 0x3b90a06, which is the
	//             next-handler-is-NULL path — that was the rejected token.
	//
	// RETIRED (do not resurrect): kCave_HandleToggleInputMode_CaveVA = 0x3b9079f /
	// Stolen = 11, and the r15-as-a_state binding that went with it. 0x3b9079f is
	// the host's shared vtable tail-dispatch: it is reached both from the early-outs
	// (where r15 == a1) and from the fallthrough (where r15 == a1+0x168), so r15 is
	// path-dependent there, and a callback that late cannot suppress the toggle at
	// all. The rel32 retarget above supersedes it on both counts.
	// =====================================================================
	inline constexpr std::uintptr_t kPatch_HandleToggleInputMode_HostVA = 0x3b905e0;
	inline constexpr std::uintptr_t kPatch_HandleToggleInputMode_SiteVA = 0x3b90780;
	inline constexpr std::size_t    kPatch_HandleToggleInputMode_SiteLen = 6;
	inline constexpr std::uintptr_t kPatch_HandleToggleInputMode_AllowVA = 0x3b90a0a;
	inline constexpr std::uintptr_t kPatch_HandleToggleInputMode_BlockVA = 0x3b90786;
	// r15 (== a1 + 0x168, i.e. &playerId) and rbx (== the InputID*) at the site.
	inline constexpr int            kPatch_HandleToggleInputMode_PlayerIdPtrReg = 15;
	inline constexpr int            kPatch_HandleToggleInputMode_InputIdReg = 3;
	inline constexpr bool           kPatch_HandleToggleInputMode_Enabled = true;

	// =====================================================================
	// (3) HandleCameraInput (win 0x141e10190) — cave in host 0x2562df0
	//
	// tracker: 0x141e10190-camera-input-shared
	// review : two adversarial passes 2026-09-03 (INSUFFICIENT ×2 on liveness
	//          prose only; the cave site, the a4 provenance and the pattern were
	//          CONFIRMED both times) + the "Dispatcher addendum (2026-09-03)"
	//          adopting the amendments as the port contract.
	// tier   : verified-adversarial
	//
	// Cave 0x25630a5, steal 7 = `48 8B 05 A4 2A 61 05`
	// (`mov rax,[rip+0x5612aa4]` -> 0x7b75b50 = ecl::EoCClient), resume 0x25630ac.
	// All four inbound edges land on the first byte. Locator
	// `0F 83 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 8D B4 24 C0 00 00 00`, 1/1, +6.
	//
	// ⚠ THE STOLEN INSTRUCTION IS RIP-RELATIVE. It is deliberately NOT replayed:
	// the cave installs with no kRestore* flag at all and the epilog re-materialises
	// the load absolutely as `mov rax, imm64(base + 0x7b75b50); mov rax,[rax]`,
	// which is semantically identical and independent of where the trampoline
	// landed. (The fork's replay path would relocate the displacement correctly
	// today — see prerequisite 3 above — but it also refuses when the trampoline
	// falls outside ±2GB of the original target, and an absolute re-materialisation
	// has neither failure mode.)
	//
	// Callback inputs at the cave:
	//   a1 (this)              = r15
	//   a4 (ls::InputID* / the input event) = [rsp+0x18] BEFORE any pushes — the
	//     host homes it once at `mov [rsp+0x18],rdx`@0x2562e01 and the slot is only
	//     re-purposed as a float from 0x25632c4 onward, well after this cave.
	//   a3 is NOT readable: it was scalarised, and via the 0x2563906 -> 0x25639f6
	//     edge rbx/r14d are path-dependent. The callback therefore takes its camera
	//     from CameraTweaks::GetCurrentCamera() (fed by Hook_UpdateCamera).
	// ALL callee-saved registers (rbp, rbx, r12-r15) are live across the region and
	// %rbp is NOT a frame pointer — it carries the high byte of the host's return
	// value to the epilogue. The generic CaveProlog/CaveEpilog never write any of
	// them, and save every caller-saved GPR, the flags and all 16 XMM registers.
	//
	// WHAT IS LOST: the verdict byte the host returns is assembled at 0x2563419,
	// downstream of the cave (byte 0 = al, byte 1 = bp), so a cave here cannot
	// change it — and every "call the original, THEN scale the result" half of the
	// Windows hook body is unreachable from a pre-body cave. See
	// NCT_Cave_HandleCameraInput in Hooks.cpp for the itemised list.
	// =====================================================================
	inline constexpr std::uintptr_t kCave_HandleCameraInput_HostVA = 0x2562df0;
	inline constexpr std::uintptr_t kCave_HandleCameraInput_CaveVA = 0x25630a5;
	inline constexpr std::size_t    kCave_HandleCameraInput_Stolen = 7;
	// r15 == a1 at the cave; Xbyak::Operand::R15.
	inline constexpr int            kCave_HandleCameraInput_ArgReg = 15;
	// Displacement off the HOST rsp of the a4 (ls::InputID*) home slot.
	inline constexpr std::int32_t   kCave_HandleCameraInput_InputEventSlot = 0x18;
	// The global the displaced rip-relative load reads (ecl::EoCClient pointer slot).
	inline constexpr std::uintptr_t kCave_HandleCameraInput_EoCClientVA = 0x7b75b50;
	inline constexpr bool           kCave_HandleCameraInput_Enabled = true;

	// =====================================================================
	// (4) AfterUpdateCameraZoom (win 0x141e1b5a0) — cave at the guest EXIT
	//
	// tracker: 0x141e1b5a0-after-update-camera-zoom
	// review : opus-5 adversarial, Attempt 2, 2026-09-03 — **CONFIRMED**. Attack 5
	//          enumerated the inbound edges (5, all landing on 0x2c7fe41 itself,
	//          zero into 0x2c7fe42..45) and audited the window (the 5 patch bytes
	//          are exactly one instruction, no rip-relative operand). Attack 6
	//          reproduced the 1/1 pattern `0F 87 ?? ?? ?? ?? 48 8B 6C 24 08 8B 45 2C`
	//          at +6.
	// tier   : verified-adversarial
	//
	// The body is scattered across the host (no contiguous guest entry), so the hook
	// goes at the guest EXIT 0x2c7fe41 — the join point where the Windows `ret` from
	// 0x141e1b5a0 lands (Win 0x141e0fba4). Steal 5 = `48 8B 6C 24 08`
	// (`mov rbp,[rsp+0x8]`), resume 0x2c7fe46. rsp-relative, so the trampoline
	// replays it after the epilog has restored rsp and it lands in rbp unchanged.
	//
	// Callback inputs:
	//   a1            = [rsp+0x18] — UpdateCamera's own arg1, stored once at
	//                   `mov [rsp+0x18],rdi`@0x2c7d3c0 (the review grepped the whole
	//                   0x83a0-byte host: exactly one store).
	//   cameraObject  = [rsp+0x8] rather than rbp. The displaced instruction is what
	//                   DEFINES rbp, so at the cave point rbp is not yet the
	//                   CameraObject — but it is loaded from [rsp+0x8], which the
	//                   review shows is written once (`mov [rsp+0x8],r15`@0x2c7d444)
	//                   and read 20× including here. Reading the slot is the same
	//                   value, one instruction earlier, with no ordering hazard.
	//   a2            = [rsp+0x38] — pinned in the CONFIRMED attempt 2026-09-04
	//                   (Attempt 1 of this row's STATUS.md): UpdateCamera's own
	//                   stack local, the Linux counterpart of the Windows hook's
	//                   `a2` (r14, reloaded once from [rbp+0x458] at
	//                   0x141e0f5c5). The Linux host writes it exactly once, at
	//                   `mov [rsp+0x38],r13`@0x2c7d43f, and it is still unmodified
	//                   at the guest exit (0x2c7fe41 sits strictly between two of
	//                   its many read-only reuses, per that attempt's exhaustive
	//                   18-occurrence classification).
	//
	// BOTH gate numbers are now pinned (kZoomAdjustContext_floorQueryArg above,
	// CONFIRMED 2026-09-07; the a2 slot below, CONFIRMED 2026-09-04) — the cave
	// passes all three inputs and CameraTweaksManager.cpp's own
	// `Known(kZoomAdjustContext_floorQueryArg)` / `GetFloorLevelLinux` gate is the
	// single remaining runtime check before AdjustCameraZoomForPitch executes.
	// =====================================================================
	inline constexpr std::uintptr_t kCave_AfterUpdateCameraZoom_HostVA = 0x2c7d3a0;
	inline constexpr std::uintptr_t kCave_AfterUpdateCameraZoom_CaveVA = 0x2c7fe41;
	inline constexpr std::size_t    kCave_AfterUpdateCameraZoom_Stolen = 5;
	// Displacements off the HOST rsp at the cave.
	inline constexpr std::int32_t   kCave_AfterUpdateCameraZoom_Arg1Slot = 0x18;
	inline constexpr std::int32_t   kCave_AfterUpdateCameraZoom_CameraObjectSlot = 0x8;
	inline constexpr std::int32_t   kCave_AfterUpdateCameraZoom_Arg2Slot = 0x38;
	inline constexpr bool           kCave_AfterUpdateCameraZoom_Enabled = true;

	// =====================================================================
	// (5) The controller zoom/pitch chain — ShouldShowSneakCones, reimplemented
	//
	// tracker: 0x14182acc0-should-show-sneak-cones (verified-adversarial, reviewer
	//          opus-5 nct-re-remainder 2026-09-03 — CONFIRMED "no standalone
	//          counterpart anywhere in .text", so the mod must reimplement it),
	//          plus 0x141b48c40-unk-singleton-ptr (table A global),
	//          0x1462d1ef0-unk-player-singleton-ptr (table B global) and
	//          0x144125ce0-get-input-value-a (the callee).
	// review : the loop shapes were re-derived instruction-by-instruction
	//          in-session 2026-09-03 from `bg3re disasm 0x2d1daf0 --around 45`
	//          (the Linux inlined copy inside host 0x2d1dad0) and
	//          `bg3re disasm 0x25c6df8 --around 14` (a standalone Linux body of
	//          the same second-table walk), against `bg3re disasm 0x14182acc0
	//          --around 50` (the Windows standalone function). Every cited
	//          instruction address appears in the comment above the code that
	//          implements it — Hooks.cpp `Offsets::ShouldShowSneakConesLinux`.
	// tier   : verified-adversarial
	//
	// No new constant is needed here: the two bucket walks are identical in shape
	// AND in offsets on both sides, and both offset sets are already pinned above
	//   table A (NCT:UnkSingletonPtr, Lin .bss 0x7c061e8) -> kSneakConeMap_* /
	//            kSneakConeNode_*, a PRESENCE test with no value byte (see the ⚠);
	//   table B (NCT:UnkPlayerSingletonPtr, Lin .bss 0x7d5c038) ->
	//            kPlayerControllerMap_* / kPlayerControllerNode_* /
	//            kPlayerControllerAbsent — the SAME walk
	//            Offsets::LookupPlayerController already implements, which is why
	//            ShouldShowSneakConesLinux calls into it instead of duplicating it;
	//   the action id -> kInputAction_ShowSneakCones (0xDC).
	//
	// ⚠ The Windows tail reads the result flag as `movzx eax,BYTE PTR [rax+8]`
	// @0x14182ad60 — out-param + 8 — because MSVC returned the aggregate through
	// memory. The Linux callee returns it BY VALUE in xmm0:rax and RE::InputValue's
	// third member `bIsPressed` IS that second eightbyte (the static_asserts in
	// Hooks.h pin the classification). So `.bIsPressed` here is the Windows
	// `[rax+8]`, not a different field.
	// =====================================================================

	// =====================================================================
	// Install-time byte gates
	//
	// Every address above is valid for exactly one game build. The site catalog's
	// identity gate (linux_build_id) is the first line of defence, but it can only
	// be as good as what gen_site_catalog.py put in the file — and the cave/patch
	// addresses here are pinned in THIS header, not fetched from the catalog. So
	// the installer also reads the bytes at each site and refuses to patch when
	// they do not match, exactly as Catalog::LooksLikeDirectCall does for the
	// write_call sites.
	//
	// Each window below is a CONTIGUOUS, WILDCARD-FREE byte run read out of
	// `bg3re disasm` in-session on 2026-09-03; `*At` is where that run starts
	// relative to the patched site (negative = before it). They are deliberately
	// LONGER than the stolen bytes: the UpdateCameraPitch review (R5) found the
	// bare 5-byte form `48 89 EF 31 F6` has 200+ whole-.text matches, including a
	// byte-identical twin at 0x2c82da4 inside this very host that reads
	// +0x13c/+0x148/+0x14c instead of +0x48/+0xF0/+0xF4.
	// =====================================================================

	// 0x2c7f8fb..0x2c7f906 = `add rbx,0x7c4` + `mov rdi,rbp` + `xor esi,esi`.
	// The site (0x2c7f902) is 7 bytes in. This is exactly the 12-byte locator the
	// review requires be used in place of the 5 stolen bytes.
	inline constexpr std::uint8_t   kSig_UpdateCameraPitch_Entry[] = {
		0x48, 0x81, 0xC3, 0xC4, 0x07, 0x00, 0x00, 0x48, 0x89, 0xEF, 0x31, 0xF6
	};
	inline constexpr std::ptrdiff_t kSig_UpdateCameraPitch_EntryAt = -7;

	// 0x2c7f961..0x2c7f96b = `mov rax,[rsp+0x78]` + `andps xmm5,xmm6` +
	// `orps xmm3,[rip+..]` (opcode+ModRM only — the rip disp32 is excluded).
	inline constexpr std::uint8_t   kSig_UpdateCameraPitch_Exit[] = {
		0x48, 0x8B, 0x44, 0x24, 0x78, 0x0F, 0x54, 0xEE, 0x0F, 0x56, 0x1D
	};
	inline constexpr std::ptrdiff_t kSig_UpdateCameraPitch_ExitAt = 0;

	// 0x25630a5..0x25630b3 = `mov rax,[rip+0x5612aa4]` + `lea rsi,[rsp+0xc0]`.
	inline constexpr std::uint8_t   kSig_HandleCameraInput_Cave[] = {
		0x48, 0x8B, 0x05, 0xA4, 0x2A, 0x61, 0x05, 0x48, 0x8D, 0xB4, 0x24, 0xC0, 0x00, 0x00, 0x00
	};
	inline constexpr std::ptrdiff_t kSig_HandleCameraInput_CaveAt = 0;

	// 0x2c7fe41..0x2c7fe4b = `mov rbp,[rsp+0x8]` + `mov eax,[rbp+0x2c]` +
	// `mov [rbp+0x20],eax`.
	inline constexpr std::uint8_t   kSig_AfterUpdateCameraZoom[] = {
		0x48, 0x8B, 0x6C, 0x24, 0x08, 0x8B, 0x45, 0x2C, 0x89, 0x45, 0x20
	};
	inline constexpr std::ptrdiff_t kSig_AfterUpdateCameraZoomAt = 0;

	// 0x3b9077d..0x3b9078c = `mov r14,r12` + `je 0x3b90a0a` + `mov rax,[r14+0x88]`
	// — the review's 1/1 locator `4D 89 E6 0F 84 ?? ?? ?? ?? 49 8B 86 88 00 00 00`
	// with its wildcard run filled in, because the original branch target is known
	// and checking it is precisely what proves we are about to rewrite the right
	// rel32. The site (0x3b90780) is 3 bytes in.
	inline constexpr std::uint8_t   kSig_HandleToggleInputMode_Site[] = {
		0x4D, 0x89, 0xE6, 0x0F, 0x84, 0x84, 0x02, 0x00, 0x00, 0x49, 0x8B, 0x86, 0x88, 0x00, 0x00, 0x00
	};
	inline constexpr std::ptrdiff_t kSig_HandleToggleInputMode_SiteAt = -3;
	// Byte offset of the rel32 operand inside the 6-byte `je` at the site, and the
	// length of the instruction the rel32 is measured from.
	inline constexpr std::ptrdiff_t kPatch_HandleToggleInputMode_Rel32At = 2;
}  // namespace NCT::LinuxLayout

#endif  // !_WIN32
