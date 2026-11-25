#pragma once

// "CursorKill" - hovering the mouse over an enemy unit destroys it.
//
// HOW THE OBJECT UNDER THE CURSOR IS FOUND
//
// The engine calls DisplayClass::DecideAction (0x692610) with the object under
// the cursor as its second argument while it works out what the cursor should
// do. That is the one place the engine itself has already resolved
// "what is under the mouse", including the layer/height rules, so this module
// taps it instead of re-implementing screen->world picking.
//
// The hook only records the pointer. The actual damage is applied in OnFrame,
// one frame later: destroying an object from inside the cursor-selection path
// would delete something the engine is still inspecting.
//
// WHAT GETS DESTROYED
//
// Only objects owned by a house that is NOT allied with the local player, and
// only infantry / units / aircraft / buildings. Everything else (terrain,
// overlays, bullets, your own units, allies, neutral) is ignored.
//
// The kill is a ReceiveDamage with a very large number and the rules'
// C4Warhead, so the engine runs its own death path (explosion, score, debris,
// "unit lost" reporting) instead of the object simply vanishing.
//
// CONFIGURATION (karst.ini)
//
//     [CursorKill]
//     Enabled=yes       ; required - anything else leaves the feature off

namespace Karst
{
	namespace Ext
	{
		namespace CursorKill
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the per-frame hook (KarstExtension::OnFrame): applies the
			// damage to everything recorded during the previous frame.
			void OnFrame();

			// Called from the hook at 0x692610 (DisplayClass::DecideAction) with
			// the object the engine found under the cursor. Records it only.
			void OnHoverObject(void* pObject);
		}
	}
}
