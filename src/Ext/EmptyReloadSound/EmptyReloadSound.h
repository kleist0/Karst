#pragma once

// "EmptyReloadSound" - plays a per-unit-type sound when a unit starts its
// empty-ammo reload.
//
// CONFIGURATION
//
// Write the key in the UNIT's own section (rulesmd.ini):
//
//     [MyUnit]
//     EmptyReloadSound=Click     ; any sound name the game knows
//
// A unit type can have its own sound, or none (leave the key out -> no sound
// for that unit). The key is read from the TechnoType of the unit doing the
// reload, which is the only reliably addressable identity here (the YRpp
// Weapon array does not match this build - measured as null).
//
// WHY THE UNIT AND NOT THE WEAPON
//
// The weapon was tried first and is not usable: TechnoTypeClass::GetWeapon
// returned null for a unit that plainly has one, so the key lives on the
// TechnoType and is read by ID.
//
// WHERE IT HOOKS
//
// 0x6FB0C9 is inside the engine routine that starts a reload timer:
//
//   0x006FB0A2  test ecx, ecx            ; ammo left?
//   0x006FB0A4  jne  0x6FB0E6            ; not empty -> normal reload
//   0x006FB0B0  cmp  [Type+0x69C], -1    ; EmptyReload configured?
//   0x006FB0B7  je   0x6FB0E6            ; no -> normal reload
//   0x006FB0C9  mov  eax, [eax+0x69C]    ; <-- hook here (6 bytes)
//
// ESI is the TechnoClass*. The routine is called repeatedly while a unit is
// reloading, so the same unit is debounced.

namespace Karst
{
	namespace Ext
	{
		namespace EmptyReloadSound
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the hook at 0x6FB0C9 with the unit whose empty reload is
			// starting.
			void OnEmptyReload(void* pTechno);
		}
	}
}
