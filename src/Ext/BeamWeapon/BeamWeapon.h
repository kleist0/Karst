#pragma once

// "BeamWeapon" - a beam that is fired from the muzzle and keeps damaging its
// target for as long as the beam is visible, like the beam rifles/cannons in
// the Gundam games.
//
// HOW IT WORKS
//
// On fire (hook 0x6FDD73, where EBX already holds the WeaponType and ESI the
// TechnoClass), a weapon that declares `LBeam=yes` gets:
//   * a LaserDrawClass from its muzzle (GetFLH) to the target, living
//     `BeamDuration` frames;
//   * an entry in an active-beam list.
// Every frame while a beam is alive, the target takes `BeamDamage` through the
// weapon's own Warhead; when the frames run out the beam is dropped.
//
// CONFIGURATION (in the weapon section)
//
//     [MyBeam]
//     LBeam=yes                ; switch
//     LBeamDuration=10         ; frames the beam lives and hurts
//     LBeamDamage=5            ; damage per frame (through the weapon's Warhead)
//     LaserInnerColor=255,255,255
//     LaserOuterColor=255,0,0
//     LaserOuterSpread=0,0,0
//
// The colours are LBeamColor; a weapon with
// LBeam=yes and no colour still works (white).
//
// IMPORTANT: this module does not remove the engine's own damage. Fire still
// resolves normally (its projectile/hit), and the beam adds the per-frame
// damage on top. A clean "only the beam hurts" version would need the engine's
// damage path suppressed, which is a separate, riskier change.

namespace Karst
{
	namespace Ext
	{
		namespace BeamWeapon
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the per-frame hook (KarstExtension::OnFrame): ticks the
			// active beams, hurting their targets.
			void OnFrame();

			// Called from the hook at 0x6FDD73: ETX = owner, EBX = weapon,
			// EBP+8 = target, EBP+0xC = weapon index.
			void OnFire(void* pOwner, void* pWeapon, void* pTarget, int weaponIndex);
		}
	}
}
