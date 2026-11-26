#pragma once

// "ReloadOverride" - gives `Reload=-1` / `EmptyReload=-1` new, useful meanings.
//
// STOCK BEHAVIOUR
//
//   Reload=       delay between reloads in frames (default 0)
//   EmptyReload=  delay used when the unit is EMPTY; -1 means "fall back to
//                 Reload" (the engine's own branch at 0x6FB0B0).
//   A negative Reload is not defined: the engine writes it into the reload
//   timer as-is, and the timer clamps a negative remaining time to 0, so it
//   behaves like Reload=0.
//
// WHAT THIS MODULE CHANGES
//
//   Reload=-1       -> normal reload is NOT started.
//   EmptyReload=-1  -> the empty reload is NOT started either: the engine no
//                      longer falls back to Reload, the unit simply does not
//                      reload.
//
// HOW (single hook)
//
//   0x6FB160 is the common return of the reload routine (both the "no reload
//   needed" path and the "normal reload" path end here; the configured
//   EmptyReload path returns at 0x6FB0E1 and is left alone). At that point the
//   timer has already been started, so this hook only replaces its duration
//   with an unreachable sentinel when the applicable value is -1:
//
//       ammo >= maxAmmo            -> nothing to do (that path also ends here)
//       ammo == 0 && EmptyReload<0 -> timer = sentinel
//       ammo > 0  && Reload<0      -> timer = sentinel
//
//   The type fields are NEVER written. An earlier version swapped EmptyReload
//   to the sentinel to force the engine down its EmptyReload branch, which
//   crashed (0xC0000005) because Phobos hooks the same routine at 0x6FB0CF
//   (TechnoClass_StartReloading_EmptyReload_Veterancy) and reads the value in
//   between.
//
// The sentinel is INT_MAX; the timer clamps a remaining time <= 0 to 0, and
// INT_MAX never reaches 0 within a match, so the unit stays "reloading" forever
// instead of refilling.

namespace Karst
{
	namespace Ext
	{
		namespace ReloadOverride
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the hook at 0x6FB160 (common return of the reload
			// routine). ESI is the TechnoClass.
			void OnReloadReturn(void* pTechno);
		}
	}
}
