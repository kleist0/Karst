#pragma once

// "BuildCost" - one [General] key that replaces EVERY TechnoType's Cost.
//
//     [General]
//     BuildCost=800
//
// With the key present, every type the engine loads - infantry, units,
// aircraft and buildings - is given this cost instead of its own `Cost=`
// value, so the whole game is priced uniformly. Without the key (or after
// removing it) the feature is completely inert: not one byte of engine state
// is written.
//
// WHY THIS HOOK POINT
//
// gamemd 1.001 reads a TechnoTypeClass' cost in exactly ONE place: the string
// "Cost" has a single reference that stores into the type's cost field
// (+0x610). That reader is shared by every type class, which is what makes a
// single hook cover all of them:
//
//     0x0071469F  mov  ecx, [ebp + 0x610]   ; current Cost, passed as default
//     0x007146A5  push ecx
//     0x007146A6  push 0x825470             ; "Cost"
//     0x007146AB  push ebx                  ; section name (the type's ID)
//     0x007146AC  mov  ecx, esi             ; CCINIClass*
//     0x007146AE  call 0x5276D0             ; CCINIClass::ReadInteger
//     0x007146B3  mov  [ebp + 0x610], eax   ; <-- hook here (6 bytes)
//
// The hook runs BEFORE the engine's own store, so putting the value back into
// EAX makes the engine store it itself: the result is indistinguishable from
// a rulesmd.ini that really said `Cost=800`.
//
// HOW THE VALUE IS READ
//
// From the very same CCINIClass the engine is reading the type's section from
// (ESI at the hook), i.e. the rules INI, section [General] - the same object
// and the same API call the engine itself uses one instruction earlier. A
// temporary INI built by a hot reload is therefore honoured as well, whereas
// reading the CCINIClass::INI_Rules singleton would keep answering with the
// copy the engine loaded at startup.
//
// WHAT FOLLOWS FROM A CHANGED Cost
//
// Cost is the engine's single source of truth for price, so everything derived
// from it moves with it; there is nothing else to patch:
//
//   * the money a player pays and the price drawn in the sidebar,
//   * the refund (RefundPercent of Cost),
//   * the AI's cost estimates,
//   * the build time, because 0x711EE0 computes
//     `(int)(Cost * RulesClass::BuildSpeed * 0.9)`.
//
// That last one is a consequence of the engine's own formula, not of this
// module: with the stock BuildSpeed=.7 a 1000-credit item takes 630 frames
// whatever the price is called, so a uniform price is also a uniform build
// time. See docs\已有功能0.1.0.md for the user-facing description.

// The engine class is only ever passed as a pointer here, so the header does
// not need to include it. It must sit at global scope: inside the namespaces
// below it would declare a different type.
class CCINIClass;

namespace Karst
{
	namespace Ext
	{
		namespace BuildCost
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the per-frame hook (KarstExtension::OnFrame). Reports how
			// many type sections have been priced, at most once per change, so the
			// log does not grow by one line per type on every rules load.
			void OnFrame();

			// Called from the hook at 0x7146B3.
			//
			// `pINI` is the CCINIClass* the engine is reading the current type's
			// section from (ESI at the hook); `section` is that section's name
			// (EBX), used for the log only.
			//
			// Returns true and fills `value` only when [General] BuildCost exists
			// and holds a sane value; the caller must then write `value` back into
			// EAX so the engine's own store instruction puts it into
			// TechnoTypeClass::Cost at +0x610.
			//
			// A missing key, an unreadable INI, a non-numeric value and a negative
			// value all return false, which leaves the engine's own answer
			// untouched.
			bool QueryOverride(void* pINI, const char* section, int& value);
		}
	}
}
