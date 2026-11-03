#pragma once

// DistributeBuild: one superweapon charge per building that carries it.
//
// Vanilla behaviour is one charge per (house, superweapon type): the engine
// creates a single SuperClass per type when the first building grants it, and
// every further building of the same type is ignored (SuperClass::Grant sees
// Granted already set). This feature lets a superweapon type opt out of that:
//
//     [IronCurtainSpecial]
//     DistributeBuild=yes
//
// Then every completed building whose BuildingTypeClass names that superweapon
// (SuperWeapon= or SuperWeapon2=) contributes its own, independently recharging
// use. The sidebar still shows one cameo: it reads "ready" while at least one
// building's charge is ready, and the progress it draws belongs to the building
// that will be ready next.
//
// Nothing here touches superweapon types that do not set the key, so existing
// SuperWeapon= / SuperWeapon2= / SuperWeapons= behaviour is unchanged.
//
// MODEL
//
//   * The engine's SuperClass for the type is kept and used as the single cameo.
//     Its IsCharged / RechargeTimer are driven from our own per-building timers
//     once per frame, so the engine's own single-timer recharge never shows
//     through.
//   * Charges are keyed by BuildingClass pointer. A building appearing (finished,
//     captured, unloaded from a save) starts a charge; one that disappears
//     (sold, destroyed) drops its charge, used or not.
//   * FIRING IS NOT HOOKED. Ares and Phobos both hook the head of
//     SuperClass::Launch (0x6CC390) and Ares also hooks SuperClass::ClickFire
//     (0x6CB920); hooks this module placed past theirs never ran in a live game.
//     Instead the module sets IsCharged = true every frame while a charge is
//     ready and treats "the engine cleared it" as a spent charge. The engine has
//     to clear it when a charge is really consumed, so this works whichever path
//     the other extensions take.
//   * When the number of ready charges changes, one line is printed into the
//     game's message list for the local player: "<ID>  ready / total".
//
// WHAT IT DOES NOT DO
//
//   * No extra cameo and no digits drawn on the cameo; the count is a message.
//   * Buildings still under construction (InLimbo) do not count.
//   * A superweapon the engine never granted (no SuperClass owned by the house)
//     is left alone.

namespace Karst
{
	namespace Ext
	{
		namespace DistributeSW
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called once per frame from KarstExtension::OnFrame().
			void OnFrame();
		}
	}
}
