// Karst - Yuri's Revenge engine extension.
//
// This file owns the DLL entry point and every Syringe hook the project
// installs. Keep the hooks here and put the actual behaviour in Karst.cpp or
// under src\Ext\, so the list of patched addresses stays readable in one place.

#include <Windows.h>

#include <Karst.h>
#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Ext/ShowByCountry/ShowByCountry.h>
#include <Ext/CountryArt/CountryArt.h>
#include <Version.h>

#include <Helpers/Macro.h>

// The CreateObject hook below needs the concrete engine classes.
// ShowByCountry.h only forward-declares BuildingTypeClass, so the real headers
// are pulled in here instead of in the shared header.
#include <GameClasses.h>
#include <BuildingTypeClass.h>
#include <HouseClass.h>

// ---------------------------------------------------------------------------
// HOOK INVENTORY
// ---------------------------------------------------------------------------
//
// Ext\IniHotReload (INI hot reload) adds NO hook: it runs from
// Karst_MainLoop -> KarstExtension::OnFrame, which is already the per-frame
// game-thread boundary a reload needs. The hook count therefore stays at 11.
// See src\Ext\IniHotReload\IniHotReload.h for why no synchronisation hook is
// required here (the reference implementation needed one only because it did
// the reload from a watcher thread).

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		// Do not call LoadLibrary, create threads or touch the heap here: the
		// loader lock is held. KarstExtension::OnGameStart() does the real setup.
		::DisableThreadLibraryCalls(hModule);
		KarstExtension::OnProcessAttach(hModule);
		break;

	case DLL_PROCESS_DETACH:
		KarstExtension::OnProcessDetach();
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	default:
		break;
	}

	return TRUE;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
//
// NOTE ON THE ADDRESS SYNTAX
// The declhook() macro inside YRpp\Syringe.h already prepends "0x" to the
// address and the size before pasting the tokens together, so the arguments
// below must be written WITHOUT a 0x prefix: writing 0x7CD810 would expand to
// the invalid literal 0x0x7CD810. (Phobos and Kratos use a newer Syringe.h that
// does not prepend 0x, which is why their DEFINE_HOOK lines look different.)
// If you ever update the YRpp copy in this project, re-check this.

// gamemd.exe entry point. By the time this runs, Syringe has already loaded
// every other extension DLL, so the engine is as ready as it will ever be.
DEFINE_HOOK(7CD810, Karst_EntryPoint, 9)
{
	return KarstExtension::OnEntryPointHook(R);
}

// Main loop, executed once per frame. Used here to post the startup message on
// the first frame where the engine can accept it. Remove this hook if you do
// not need per-frame work - every hook costs a little frame time.
DEFINE_HOOK(55D360, Karst_MainLoop, 5)
{
	KarstExtension::OnFrame();

	// A SEPARATE feature (Ext\CountryArt). Inert - no allocation, no engine call -
	// unless some building declares ShowByCountries, and it reads none of the
	// ShowByArt state above.
	Karst::Ext::CountryArt::OnFrame();

	return 0;
}

// Where a building actually comes from - all four sites, at once.
//
// The executable allocates the 0x720-byte BuildingClass in exactly four places,
// each followed by a call to its constructor (0x43B740):
//
//   0x44FAF8 -> 0x44FB22      measured: 25 hits, all of them the map's preplaced
//   0x44FD6E -> 0x44FD8A      scenery. A deployed construction yard did NOT come
//   0x45E806 -> 0x45E81F      through here, so it must use one of the others.
//   0x45E883 -> 0x45E89C
//
// All four now report, so one run with an actual deploy identifies the path.
// Each hook only logs; nothing is modified.
DEFINE_HOOK(44FAF8, karst_ShowByCountry_Alloc1, 5)
{
	Karst::LogInfo(L"ShowByCountry[alloc]: site 1 (0x44FAF8) reached.");
	return 0;
}

DEFINE_HOOK(44FD6E, karst_ShowByCountry_Alloc2, 5)
{
	Karst::LogInfo(L"ShowByCountry[alloc]: site 2 (0x44FD6E) reached.");
	return 0;
}

DEFINE_HOOK(45E806, karst_ShowByCountry_Alloc3, 5)
{
	Karst::LogInfo(L"ShowByCountry[alloc]: site 3 (0x45E806) reached.");
	return 0;
}

DEFINE_HOOK(45E883, karst_ShowByCountry_Alloc4, 5)
{
	Karst::LogInfo(L"ShowByCountry[alloc]: site 4 (0x45E883) reached.");
	return 0;
}

// ---------------------------------------------------------------------------
// ShowByCountries / ShowBySHP
// ---------------------------------------------------------------------------
//
// The image is applied by hooking the routine the engine uses to (re)load a
// building type's ART section, 0x45F230.
//
// Why here rather than from the main loop:
//
//   0x45F230 reads the `Image=` key and, when it is absent, resets ImageFile to
//   the type's default section name before loading. Applying the rule from the
//   main loop therefore got overwritten on the next ART load - the log showed
//   "already points at NACNST" while the building still drew gacnst_a. Writing
//   ImageFile immediately *before* the engine reads it is the only point where
//   the value cannot be undone.
//
// The hook runs on the type (ECX = BuildingTypeClass*). It looks up the rule by
// the type's index, resolves the owning country, and writes the target section
// name into ImageFile. Everything else - main image and every animated piece -
// then comes from that section because the engine is about to load it.





// ---------------------------------------------------------------------------
// Creation-time observation  (read-only)
// ---------------------------------------------------------------------------
//
// WHY THESE EXIST
//
// The country sync changes animation *names* on BuildingTypeClass, and the log
// proves it works: after a sync the type reads 'NACNST_A' / 'NACNST_C' /
// 'NACNST_B'. Yet a live match drew the old animations. A building's animation
// objects live on the *instance* (array at +0x55C) and are created from the type
// when the building is constructed, so the open question is:
//
//     at the moment a building is created, does its type already carry the
//     country's names, or the original GACNST ones?
//
// If it already carries the country's names, the sync only has to run earlier
// (or the engine rebuilds the instance objects on its own). If it still carries
// GACNST's, something re-reads the section after the sync.
//
// These hooks only read and log. Nothing is modified - an earlier attempt to
// rebuild the instance anims crashed inside the Ares/Phobos module, so the next
// step must be decided from evidence rather than by trying another write.

// The engine's own "play anim slot N of this building" helper, 0x451750.
//
// This is the cleanest possible probe of the type-side field mapping, because
// the engine computes the address itself:
//
//   0x00451765  mov edi, [esi + 0x520]         ; Type
//   0x00451772  lea edx, [edi + edx*4 + 0xF5C] ; Type + 0xF5C + slot*0x3C
//
// So logging `slot` and the type pointer here says exactly which member the
// engine would read for each animation slot - no guessing about array bases.
// The type-side names have already been synced by the time this runs, so the
// logged address also shows whether the engine sees the country's name.
// ---------------------------------------------------------------------------
// Creation-time observation  (read-only)  --  DISABLED
// ---------------------------------------------------------------------------
//
// TWO HOOKS WERE TRIED HERE AND BOTH CRASHED THE GAME. They are deliberately not
// installed, and the reasoning is kept so they are not reattempted blindly:
//
//   0x451750  The engine's per-slot "play anim" helper. A hook with size 6 was
//             installed; the game died with C0000005 at a garbage EIP. Syringe
//             trampolines require the size to cover WHOLE instructions, and the
//             prologue boundary is 1 (`push ebx`) + 4 (`mov ebx,[esp+0x10]`) = 5,
//             so 6 cut into `push esi`. Whether that alone explains the crash is
//             unproven - Ares/Phobos may also hook this address, which would make
//             it unusable for us regardless.
//
//   0x44FB27  Just after a BuildingClass constructor returns. Also crashed.
//
// The lesson is recorded in HANDOFF.md: this DLL already has two hooks that have
// been stable for the whole project (the entry point 0x7CD810 and the main loop
// 0x55D360). Every additional hook is a new way to corrupt the game, and the
// observation these two were meant to make is obtainable without any hook at all
// - by reading instance memory from the existing main-loop hook, which is what
// ReportInstanceAnims does.
//
// For reference, the addresses that were being probed:
//   0x451750  per-slot anim helper; computes Type + 0xF5C + slot*0x3C
//             (0x451765 `mov edi,[esi+0x520]`, 0x451772 `lea edx,[edi+edx*4+0xF5C]`)
//   0x44FB22  call to the BuildingClass constructor at one of four sites
//   0x44FB27  first instruction after that call

// ---------------------------------------------------------------------------
// ART section loader  (0x45F230)  --  NOT HOOKED, and why
// ---------------------------------------------------------------------------
//
// Hooking this was the plan for covering the whole ART vocabulary at once: write
// ImageFile, let the engine re-read the section, and every key (Buildup,
// SuperAnim*, ActiveAnim*, ...) follows automatically without enumerating any of
// them.
//
// It does not work, and the disassembly says exactly why. The type's full ART
// reader reads its keys BEFORE it calls this loader:
//
//   0x0046155A  push 0x81a60c        ; "Buildup"        <- key read here
//   0x00461575  call 0x528a10        ; read value from the current section
//   0x004615A3  rep movsd            ; write it into [ebp+0xE5C] (BuildupFile)
//   0x004615C5  call 0x45F230        ; only NOW is the ART loader called
//
// By the time a hook on 0x45F230 runs, Buildup (and every other key that reader
// handles) has already been read and bound. Changing the section name there can
// only affect keys this loader itself reads, which the same scan shows are just:
//
//   Image, BibShape, SpecialZOverlay, UnderRoofDoorAnim, Rubble,
//   UnderDoorAnim, DoorAnim, RoofDeployingAnim, DeployingAnim
//
// So the loader hook is removed rather than left in place looking like it does
// something it cannot.
//
// The real constraint: a building type's ART is fully parsed ONCE, during
// startup, BEFORE the main loop and before any country rule can be known. Keys
// bound at that moment cannot be redirected afterwards; only keys the engine
// re-reads at draw time (the main Image) can be changed at runtime. That is
// exactly the split observed in practice - the main image switches, everything
// else stays on the source section.

// ---------------------------------------------------------------------------
// REMOVED: Draw-time art selection  (BuildingClass::GetImage, 0x4513D0)
// ---------------------------------------------------------------------------
//
// This hook returned a per-country type clone at draw time, i.e. it was one of
// the two mechanisms behind "show a building as its owner's country". Both are
// gone now:
//
//   * it needed per-country type clones, and a clone was built as
//     `memcpy(source) + ImageFile=<target section>` - so it carried the TARGET
//     section's art together with the SOURCE type's animation names. That is
//     what made the activity animations wrong, and it is why the per-country
//     path was disabled before.
//   * it also duplicated the construction-time swap, so a single building was
//     being redirected twice, once per frame.
//
// The global ShowByArt path (rename the type's ART section before the engine
// parses it) needs no clone and no draw hook, and is what this DLL uses now.



// Two diagnostic probes were removed here (0x4518C1 playback loop and 0x451A2F
// Anims[] store). Their findings are in HANDOFF.md: the engine creates animation
// objects for other buildings such as CAWASH18 but never for GACNST, and Anims[]
// stays empty, so the playback loop never runs.

// ---------------------------------------------------------------------------
// REMOVED: AnimClass::Draw hook  (0x423AC0) -- per-animation country visibility
// ---------------------------------------------------------------------------
//
// This hooked the animation draw path and marked an animation Invisible when the
// owner's country was not listed on the animation's own art section. It was part
// of the per-country feature and is gone with it (it also depended on the same
// clone/owner lookups).


// ---------------------------------------------------------------------------
// BuildingClass constructor  (0x43B740)  --  DIAGNOSTIC
// ---------------------------------------------------------------------------
//
// This is the single point where a building's Type is written for the first time:
//
//   0x0043B740  mov  eax, [esp + 8]        ; (arg)
//   0x0043B750  mov  ecx, [esp + 0x14]     ; the BuildingTypeClass*
//   0x0043B756  mov  [esi + 0x520], ecx    ; Type <- set here
//
// There are only four call sites in the whole binary (0x44FB22, 0x44FD8A,
// 0x45E81F, 0x45E89C), yet live logs show only the first is ever reached - and
// only for map scenery. A deployed construction yard never appears in those
// logs, so it must be created through a path that does not pass through them.
//
// Hooking the CONSTRUCTOR rather than the call sites therefore sees every
// BuildingClass that is ever created, and this log says which ones those are.
// It is read-only: the constructor runs unchanged.
//
// SIZE 5 == `mov eax,[esp+8]` (4) + `push ebx` (1).
// The constructor-entry probe is retained but disabled: reading Type from the
// stack kept producing unreadable pointers, while the store instruction below
// gives both operands exactly. Kept as documentation of what was tried.
#if 0
DEFINE_HOOK(43B740, karst_ShowByCountry_BuildingCtor, 5)
{
	// __thiscall: ECX = the new BuildingClass. The Type is the FIRST STACK
	// ARGUMENT, which the constructor itself reaches like this:
	//
	//   0x0043B744..0x0043B747  push ebx / ebp / esi / edi   ; ESP -= 16
	//   0x0043B750  mov ecx, [esp + 0x14]   ; = [entryESP + 4] after those pushes
	//   0x0043B756  mov [esi + 0x520], ecx  ; Type
	//
	// A first version read [esp + 0x14] at hook time, where ESP is still the entry
	// value, so it picked up an unrelated stack slot - which is why every line
	// logged `type=00000000`.
	const DWORD* typeArg = reinterpret_cast<const DWORD*>(R->ESP() + 4);

	static int ctorLogsLeft = 40;

	if (ctorLogsLeft > 0)
	{
		--ctorLogsLeft;

		DWORD typeValue = 0;
		if (typeArg && !IsBadReadPtr(typeArg, sizeof(DWORD)))
			typeValue = *typeArg;

		// Log BOTH candidate name fields, raw, so the correct one is settled from
		// data rather than assumed:
		//   +0x24  AbstractTypeClass::ID      (the type's name, e.g. "GACNST")
		//   +0x1F8 ObjectTypeClass::ImageFile (the ART section the engine loads)
		char idText[32] = { 0 };
		char sectionText[32] = { 0 };

		if (typeValue >= 0x400000 && typeValue < 0x7E4000)
		{
			const char* id = reinterpret_cast<const char*>(typeValue + 0x24);
			if (!IsBadReadPtr(id, 1) && id[0] >= 0x20 && id[0] < 0x7F)
				strncpy_s(idText, id, _TRUNCATE);

			const char* imageFile = reinterpret_cast<const char*>(typeValue + 0x1F8);
			if (!IsBadReadPtr(imageFile, 1)
				&& imageFile[0] >= 0x20 && imageFile[0] < 0x7F)
			{
				strncpy_s(sectionText, imageFile, _TRUNCATE);
			}
		}

		wchar_t wideId[64] = { 0 };
		wchar_t wideSection[64] = { 0 };

		if (idText[0])
			MultiByteToWideChar(CP_ACP, 0, idText, -1, wideId, 64);

		if (sectionText[0])
			MultiByteToWideChar(CP_ACP, 0, sectionText, -1, wideSection, 64);

		Karst::LogInfo(L"ShowByCountry[ctor]: this=%p type=%p ID='%ls' ImageFile='%ls'",
			reinterpret_cast<void*>(R->ECX()),
			reinterpret_cast<void*>(typeValue),
			wideId[0] ? wideId : L"<bad>",
			wideSection[0] ? wideSection : L"<bad>");
	}

	return 0;
}
#endif


// ---------------------------------------------------------------------------
// Sidebar MIX selection -- READ-ONLY probe.
//
// THE ENGINE'S PATH (measured in gamemd 1.001)
//
//   0x0067E74A  mov  eax, [0xa8b230]      ; ScenarioClass::Instance
//   0x0067E75B  mov  ecx, [eax + 0x34B8]  ; PlayerSideIndex  <- SIDE, not country
//   0x0067E761  call 0x534FA0             ; -> "SIDEC%02dMD.MIX"
//
//   0x00534FA0  sub  esp, 0x11c           ; entry                <- probe 1
//   0x00534FAA  mov  esi, ecx             ; esi = side index
//   0x00534FB1  cmp  esi, 2               ; <- Ares hooks HERE (5 bytes)
//   0x00534FB4  jne  0x534FBB
//   0x00534FB6  mov  esi, 1               ; stock: Yuri counts as 1
//   0x00534FBB  mov  ebx, esi
//   ...
//   0x005350D3  lea  esi, [ebx + 1]       ; ★ landing point
//   0x005350D6  lea  eax, [esp + 0x14]
//   0x005350DA  push esi
//   0x005350DB  push 0x827e0c             ; "SIDEC%02dMD.MIX"
//   0x005350E1  call 0x7c8ef4             ; sprintf -> the actual file name
//
// WHY 0x5350D3 AND NOT 0x534FBB
//
// The entry probe fired (`sideIndex(ECX)=0`) while the 0x534FBB probe never did,
// proving the engine DOES call this function and that Ares' hook does not resume
// at 0x534FBB despite its published source saying `return 0x534FBB`. 0x5350D3 is
// the convergence point inside the same function where the side index finally
// becomes the file name, and it is far from Ares' 5-byte site at 0x534FB1.
//
// SIZE MUST COVER 7 BYTES, NOT 3
//
// The first attempt declared size 3, i.e. only `lea esi,[ebx+1]`. Syringe writes
// a 5-byte `jmp`, which then swallowed the first 2 bytes of the following
// `lea eax,[esp+0x14]`; resuming at 0x5350D6 ran a corrupted instruction and the
// stack arguments to sprintf went wrong - crash C0000005 at 0x7CE9F6.
//
// 3 + 4 = 7 covers both `lea`s on their instruction boundary, which is the
// smallest legal size here. Every hook in this file must be >= 5 bytes: that is
// the jmp, and tools/hookcheck.py now enforces it.
//
// Reads only.
DEFINE_HOOK(534FA0, karst_ShowByCountry_SidebarMixEntry, 6)
{
	Karst::Ext::ShowByCountry::ProbeSidebarMixEntry(
		static_cast<unsigned long>(R->ECX()));

	return 0;
}

// ---------------------------------------------------------------------------
// Per-COUNTRY sidebar MIX  (0x5350D3)  -- the actual feature
// ---------------------------------------------------------------------------
//
// 0x5350D3 is where the engine finally turns a side index into a file name:
//
//   0x005350D3  lea  esi, [ebx + 1]    ; side index + 1
//   0x005350D6  lea  eax, [esp+0x14]
//   0x005350DA  push esi
//   0x005350DB  push 0x827e0c          ; "SIDEC%02dMD.MIX"
//   0x005350E1  call 0x7c8ef4          ; sprintf
//
// Ares hooks 0x534FB1 and rewrites ESI from its own per-SIDE setting. Karst hooks
// here instead, far away from Ares' 5 bytes, and rewrites EBX when the player's
// COUNTRY declares `Sidebar.MixFileIndex`. Because `lea esi,[ebx+1]` runs with the
// substituted EBX, the engine builds the country's MIX name itself.
//
// Only the name is changed: loading the MIX, building the sidebar and drawing the
// cameos all remain the engine's own work.
//
// SIZE MUST COVER 7 BYTES (3 + 4), not 3.
//
// Declaring 3 (just `lea esi,[ebx+1]`) let Syringe's 5-byte jmp eat the first two
// bytes of the following `lea eax,[esp+0x14]`; resuming at 0x5350D6 then ran a
// corrupted instruction and sprintf got garbage stack arguments - C0000005 at
// 0x7CE9F6. tools/hookcheck.py now rejects any hook smaller than 5 bytes.
DEFINE_HOOK(5350D3, karst_ShowByCountry_CountrySidebarMix, 7)
{
	int mixIndex = 0;

	if (Karst::Ext::ShowByCountry::CountrySidebarMixIndex(mixIndex))
	{
		// The engine adds 1 back (`lea esi,[ebx+1]`), so subtract it here: a rule
		// of 2 must produce SIDEC02MD.MIX.
		R->EBX(mixIndex - 1);
	}

	return 0;
}

// ---------------------------------------------------------------------------
// Full ART-section parser  (0x45FE50)  --  section redirect, before it reads
// ---------------------------------------------------------------------------
//
// This is the engine's COMPLETE ART reader, and it is where a type's section name
// is finally consumed:
//
//   0x0045FE50  sub esp, 0x358
//   0x0045FE5F  mov ebp, ecx             ; EBP = the BuildingTypeClass
//   0x0045FE80  lea esi, [ebp + 0x24]    ; ID
//   0x00461608  lea edi, [ebp + 0x1F8]   ; ImageFile, read ~6 KB later
//   0x004617C4  push "ActiveAnim"        ; ...and every key is read from it
//
// Setting ImageFile at the entry means the engine parses the target section and
// fills every key itself - Image, Buildup, ActiveAnim, IdleAnim, ... - so the
// building's animation objects are created by the engine from its own data, with
// its own position and palette. Nothing is copied or drawn by this DLL.
//
// Measured reason this timing is required: with the section untouched the engine
// creates the construction yard's activity animation (Anims[] went 0 -> 1 after
// deployment); when the section was rewritten later from the main loop it created
// none, because the names had already been parsed from the old section and the
// partial loader at 0x45F230 never refreshes them.
//
// SIZE 6 == `sub esp, 0x358`.
DEFINE_HOOK(45FE50, karst_ShowByCountry_BeforeArtParse, 6)
{
	Karst::Ext::ShowByCountry::RedirectSectionBeforeFullParse(
		reinterpret_cast<void*>(R->ECX()));

	// A SEPARATE feature, sharing this hook only because Syringe cannot put two
	// hooks on one address. It neither reads nor changes anything ShowByArt owns:
	// it looks at the type and, only when that type declares ShowByCountries, it
	// makes (or remakes, when a new match gave the type a new object) its own
	// per-country copies and parses them with the very same INI the engine is
	// using. The first stack argument is that CCINIClass*.
	Karst::Ext::CountryArt::OnArtParse(
		reinterpret_cast<void*>(R->ECX()),
		reinterpret_cast<void*>(R->Stack32(4)));

	return 0;
}


// ---------------------------------------------------------------------------
// Animation lifecycle tracing
// ---------------------------------------------------------------------------
//
// WHY THERE IS ONLY ONE PROBE
//
// Three extra hooks were added in the previous build (0x451890 entry, 0x451E40
// entry, 0x4509DE) and the game died at load twice with
// `C0000005 at 0x00451898` - the SAME address both times, even after the
// 0x4509DE hook size was corrected from 2 to 6 and tools\hookcheck.py reported
// every hook on an instruction boundary. Hooking in 0x451890's neighbourhood is
// therefore not safe in this install (Ares/Phobos very likely hook the same
// routine; the project notes already warn that a second hook in one function is
// a new crash opportunity). All three were removed.
//
// The question they were meant to answer is still answerable from the ONE hook
// that has been stable for many rounds - the Anims[] store at 0x451A2F, which
// is the only instruction in gamemd that ever puts an animation object into a
// building's slot array. ReportAnimStored now prints the construction yard
// unconditionally, so the log itself says whether slot 3 (ActiveAnim) is ever
// created for it:
//
//   * no `[yardstore] ... slot=3` line, and Anims[3] stays null
//       -> no call site ever requests it; the search moves to the 70 call sites.
//   * `[yardstore] ... slot=3` appears but Anims[3] is null afterwards
//       -> it was created and then destroyed (0x450B93, or its twin 0x450C3D).
//
// Read-only, no new hook, no new crash risk.

// BuildingTypeClass::CreateObject
//
// A hook at this address (0x45E800) was tried in order to swap in a
// country-specific type clone at creation time. It crashed the game on the
// loading screen even with the clone path disabled, so the hook itself - not the
// clone code - was at fault. It is deliberately NOT installed.
//
// The requirement it was meant to serve still stands, and the deploy-timing hook
// above is the better understood point: there the building already exists with
// its Type and Owner set, which CreateObject's first instruction does not offer.

// ---------------------------------------------------------------------------
// Per-COUNTRY building art  (ShowByCountries / ShowBySHP)  --  Ext\CountryArt
// ---------------------------------------------------------------------------
//
// This hook belongs to Ext\CountryArt, which is independent of the ShowByArt
// code above: it has its own state, its own INI keys and its own type copies,
// and it changes nothing ShowByArt owns. It is inert unless a building declares
// ShowByCountries.
//
// The hooked instruction is the single point at which a BuildingTypeClass* is
// put on a live BuildingClass:
//
//   0x0043B750  mov  ecx, [esp + 0x14]     ; the constructor's second argument
//   0x0043B756  mov  [esi + 0x520], ecx    ; <- Type
//
// The constructor is `BuildingClass::BuildingClass(BuildingTypeClass* pType,
// HouseClass* pOwner)` - all four call sites agree: the owner is pushed first,
// the type last. The instance's own Owner field is not written yet at this
// point, so the owning country has to come from that second argument.
//
// ESP at the hook is the entry ESP minus 0x10 (four pushed registers, plus the
// base constructor popping its own argument), so [ESP+0x14] is the type -
// exactly the slot the engine loads into ECX on the line above - and [ESP+0x18]
// is the owner.
//
// SIZE 6 == `mov [esi+0x520], ecx`, and 0x43B75C is the next instruction, so
// both hook rules (>= 5 bytes, whole instructions) hold.
//
// A country that declares nothing, and map scenery (which passes no owner at
// all), leave ECX exactly as the engine set it.
DEFINE_HOOK(43B756, karst_CountryArt_TypeStore, 6)
{
	void* const type = reinterpret_cast<void*>(R->ECX());
	void* const owner = reinterpret_cast<void*>(R->Stack32(0x18));

	void* const chosen = Karst::Ext::CountryArt::TypeForOwner(type, owner);

	if (chosen && chosen != type)
		R->ECX(reinterpret_cast<DWORD>(chosen));

	return 0;
}

// ---------------------------------------------------------------------------
// Side-bar icon  (TechnoTypeClass::GetCameo, 0x712040)  --  Ext\CountryArt
// ---------------------------------------------------------------------------
//
// WHY THE ICON NEEDS A SECOND HOOK
//
// The side bar lists what the player can build straight out of
// BuildingTypeClass::Array, and what it stores per entry is a TYPE INDEX, not a
// pointer to the copy a building will eventually use. The icon of a listed
// building is therefore read from the SOURCE type object - the one every
// country shares - and copying the cameo fields onto a per-country copy cannot
// reach it: the player kept seeing the source building's icon next to a
// building that already drew the target section's art (measured).
//
// The engine's cameo lookup is where that reading happens:
//
//   0x712040  mov  eax,[0xa83d4c]   ; HouseClass::Player (the LOCAL player)
//   0x712045  push esi
//   0x712048  mov  esi,ecx          ; this = the type being asked about
//   ...
//   0x7120BF  mov  eax,[esi+0x6f0]  ; Cameo
//   0x7120C5  pop  esi / ret
//
// This hook replaces ECX - the type the lookup is about to answer for - with
// the copy belonging to the LOCAL player's country before the body runs.
// Everything after that already works through `esi`, so the whole lookup,
// including the AltCameo branch at +0x710, answers from the copy's own fields -
// and the engine keeps drawing: this DLL still draws nothing.
//
// Only types this module configured are touched, only when the local player's
// country is known, and only when the copy actually carries an icon; every
// other caller - menu screens, other houses, other buildings - keeps the
// engine's own answer.
//
// SIZE MUST BE EXACTLY 5, i.e. `mov eax,[0xa83d4c]` and nothing more.
//
// Ares.dll hooks this same function at 0x712045 ("TechnoTypeClass_GetCameo",
// size 5), so covering one byte further overlaps Ares' trampoline. Measured with
// size 6: `C0000005 at 0x71204D` - the middle of the following instruction - the
// moment a construction yard was deployed, because the two hooks resumed inside
// one another's code. tools\hookconflict.py now checks every hook here against
// Ares, Phobos and Kratos before a build is deployed.
//
// 0x712045 happens to be `push esi`, so the two hooks are adjacent rather than
// overlapping and still chain: this one runs first and replaces ECX, Ares' runs
// next with the replaced value already in place, and the `mov esi,ecx` that Ares
// restores then loads the copy.
DEFINE_HOOK(712040, karst_CountryArt_Cameo, 5)
{
	void* const chosen = Karst::Ext::CountryArt::TypeForCameo(
		reinterpret_cast<void*>(R->ECX()));

	if (chosen)
		R->ECX(reinterpret_cast<DWORD>(chosen));

	return 0;
}

