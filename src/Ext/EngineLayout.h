#pragma once

// Member offsets inside gamemd.exe's classes, with the address each one came
// from.
//
// They are duplicated here rather than pulled from YRpp because this module is
// deliberately header-light: the offsets are what the runtime hooks actually
// read, so stating them in one place - together with how they were verified -
// keeps the assumptions reviewable.
//
// All of these were confirmed by disassembling gamemd.exe 1.001 with
// tools\dumpscan.py and tools\findmember.py.

namespace Karst
{
	namespace Ext
	{
		namespace EngineLayout
		{
			// BuildingClass::Type  (BuildingTypeClass*)
			//   0x43B756  mov [esi+0x520], ecx      ; ecx = pType
			constexpr unsigned Building_Type = 0x520;

			// BuildingClass::OwnerCountryIndex (DWORD)
			//   0x43B76B  mov [esi+0x528], edx      ; edx = [0xA8ED84]
			//   YRpp names this OwnerCountryIndex. It is initialised from a global
			//   at construction time, so it is NOT trusted at draw time; the
			//   owner chain below is used instead.
			constexpr unsigned Building_OwnerCountryIndex = 0x528;

			// HouseClass member holding the country (HouseTypeClass*).
			//
			// YRpp claims 0x674, but disassembly shows [reg+0x674] used with a COM
			// calling pattern (null check, push 0x80004003 = E_POINTER, call through
			// [ecx+0x18]) - that is one of HouseClass's embedded COM interfaces, not
			// the country. The country pointer sits near the start of the object.
			constexpr unsigned House_Type = 0x34;

			// ObjectClass::IsSelected (bool)
			//   Sits just after InOpenToppedTransport; see ObjectClass.h.
			constexpr unsigned Object_IsSelected = 0x84;

			// --- Re-loading a type's ART section ---------------------------
			//
			// 0x45F230 walks a BuildingTypeClass's ART section and binds
			// everything in it - the main SHP *and* the animated pieces
			// (ActiveAnim, ActiveAnimDamaged, IdleAnim, ...). Replacing only the
			// main SHP leaves the animations pointing at the previous section,
			// which is what produced a half-built looking structure.
			//
			// The function opens with a "already loaded" test and skips the whole
			// reload when that byte is set:
			//
			//   0x45F23E  cmp byte ptr [ebp+0x1761], bl
			//   0x45F244  jne 0x45F310
			//
			// so the flag has to be cleared for a reload to happen at all.
			constexpr unsigned LoadArtSectionFunction = 0x45F230;
			constexpr unsigned Type_ArtLoadedFlag = 0x1761;

			// The loader takes ONE argument: the theater to load for. The engine
			// reads it from ScenarioClass::Instance + this offset:
			//
			//   0x4652DD  mov ecx, [eax+0x1258]   ; eax = ScenarioClass::Instance
			//   0x4652E3  push ecx
			//   0x4652E6  call 0x45F230
			//
			// Calling the loader without it unbalances the stack and crashes.
			constexpr unsigned Scenario_Theater = 0x1258;

			// A second flag the engine clears immediately before calling the
			// loader; see the call site at 0x45EB2C. Clearing only the first flag
			// left the loader in an unexpected state and it raised an exception.
			constexpr unsigned Type_ArtReloadFlag = 0x0E04;

			// TechnoClass::Owner  (HouseClass*)
			//   Not set by the constructor - 0x6F2B6F zeroes it - so it is only
			//   valid after the object has been placed. Written at 0x41E3FB,
			//   0x41FE17 and 0x41FED3.
			//
			//   TO BE TREATED WITH SUSPICION: a live match read 0 or 1 from 0x108
			//   on a deployed construction yard, i.e. sentinel values rather than a
			//   pointer. The owning house is therefore discovered at runtime, the
			//   same way the country member is, and the search window below is what
			//   that search covers.
			constexpr unsigned Techno_Owner = 0x108;

			// Owning-house search window, in bytes from the object start.
			//
			// The documented 0x108 held 0 or 1 on a deployed construction yard, and
			// a 0x0F0..0x130 window found nothing either, so the window now covers
			// the whole plausible header region of the object. The acceptance test
			// is strict (the candidate must be a live engine object whose own
			// country member resolves to a HouseTypeClass), so a wider window costs
			// only scan time, not correctness.
			constexpr unsigned OwnerSearchBegin = 0x0;
			constexpr unsigned OwnerSearchEnd = 0x600;
			constexpr unsigned OwnerSearchStep = 4;

			// How many times the owner search may run before it gives up for the
			// match. Each attempt costs (window / step) * HouseClass::Array.Count
			// memory probes, so an unbounded retry loop stalls the game - which is
			// exactly what happened with an earlier revision.
			constexpr int OwnerSearchMaxAttempts = 3;

			// TechnoClass::InitialOwner (HouseClass*)
			//   Sits directly in front of Owner.
			constexpr unsigned Techno_InitialOwner = 0x104;

			// HouseClass::Type / Country - the pointer to the owning
			// HouseTypeClass (the country).
			//
			// THIS OFFSET IS NOT TRUSTWORTHY. YRpp claims 0x674, but
			// disassembling gamemd.exe shows the accesses at [reg+0x674] using a
			// COM pattern (null check, push 0x80004003 = E_POINTER, then a call
			// through the vtable at [ecx+0x18]); 0x674 is one of HouseClass's
			// embedded COM interfaces, matching the IConnectionPointContainer
			// comment in HouseClass.h. The country therefore lives somewhere
			// else.
			//
			// Rather than hard-code a second guess, the runtime searches the
			// object for the offset that yields a real HouseTypeClass*, within
			// the window below. That window excludes the COM interface block, so
			// the search cannot pick one of those by mistake.
			//
			// Set CountrySearchEnable to false once the offset has been confirmed
			// on a real build, and fill in CountryOffsetVerified.
			constexpr bool CountrySearchEnable = true;

			// Search window, in bytes from the start of the HouseClass.
			constexpr unsigned CountrySearchBegin = 0x28;
			constexpr unsigned CountrySearchEnd = 0x9C;
			constexpr unsigned CountrySearchStep = 4;

			// Fill this in (and disable the search) once a run of the game has
			// confirmed the offset; the diagnostic log prints whatever the search
			// settles on.
			constexpr unsigned CountryOffsetVerified = 0x0;

			// gamemd.exe's executable range, read from its PE header:
			//   .text  va=0x00401000  vsize=0x003DF38D
			// Used to sanity-check a pointer before dereferencing it: a live
			// engine object always starts with a vtable pointer that points into
			// this range. This is a cheap way to reject a stale or misread
			// pointer without trusting another unverified offset.
			constexpr unsigned Process_TextStart = 0x00401000;
			constexpr unsigned Process_TextEnd = 0x007E038D;
		}
	}
}
