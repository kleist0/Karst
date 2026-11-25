#pragma once

// "UnitTakeover" - select any unit (neutral / friendly / enemy) and press
// Alt+X to make it yours.
//
// WHAT IT DOES
//
// Every frame it watches for Alt+X. On the frame the combination goes down it
// walks the engine's techno list, takes every object with IsSelected set, and
// calls TechnoClass::SetOwningHouse(HouseClass::Player) on it. That is the
// engine's own ownership transfer (the same call that handles a captured
// building), so the object's owner field, the two houses' bookkeeping and the
// minimap colour all move with it.
//
// No hook: the polling rides on KarstExtension::OnFrame, which is already the
// per-frame game-thread boundary.
//
// CONFIGURATION (karst.ini)
//
//     [UnitTakeover]
//     Enabled=yes       ; required - anything else leaves it off
//
// The key check is edge-triggered (only the frame the combination is pressed),
// so holding Alt+X does not re-apply every frame.

namespace Karst
{
	namespace Ext
	{
		namespace UnitTakeover
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the per-frame hook (KarstExtension::OnFrame): polls the
			// key combination and transfers the selection.
			void OnFrame();
		}
	}
}
