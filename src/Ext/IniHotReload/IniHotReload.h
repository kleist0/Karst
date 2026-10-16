#pragma once

// IniHotReload, part 3 of 3 - rereading the watched INI files while the game runs.
//
// WHAT IT DOES
//
// When one of the files listed in <game directory>\karst.hotreload.ini is saved
// while the game is running, the engine's type objects are refed the INI data
// from that file, in place, without restarting the game. Nothing is written back
// to disk and nothing survives the process: this is an authoring aid for the
// current session, not a change to the mod.
//
// WHY IT NEEDS NO HOOK OF ITS OWN
//
// The upstream reference implementation (DynamicLoadINI.cs) has to
// stop the game thread with a semaphore, because it runs the reload on a
// watcher thread. Karst already runs code on the game thread once per frame
// (the existing main loop hook at 0x55D360 -> KarstExtension::OnFrame), which is
// exactly the safe point it needs. This module consequently adds NO hook at all
// - the project's hook count must stay at 11.
//
// WHY THE ENGINE'S OWN INI OBJECTS ARE LEFT ALONE
//
// The reload builds a TEMPORARY CCINIClass and rereads the files into it, then
// asks each type object to load itself from that temporary object. The engine's
// singletons (CCINIClass::INI_Rules and friends) are never replaced: Ares,
// Phobos and Kratos identify them by pointer, and swapping one out would break
// those identity checks.
//
// WHAT THIS FIRST VERSION DELIBERATELY DOES NOT DO
//
//   * It does not retype global scalar sections ([General], [CombatDamage], ...)
//     - that would need RulesClass::Read_File, which the extensions also hook.
//   * It does not touch existing INSTANCES. Only the type objects are refed, so
//     a field an instance copied at construction time keeps its old value.
//   * It never retypes TerrainType: the reference implementation measured a
//     crash there.
//
// See docs\karst使用.md for the user-facing description and the limits.

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			// Reads karst.hotreload.ini (if present and enabled) and starts the
			// watcher thread. Called once from KarstExtension::OnGameStart(),
			// i.e. on the game thread after the loader lock is gone.
			void Init();

			// Called once per frame from KarstExtension::OnFrame(). Costs one
			// relaxed atomic read while nothing changed.
			void OnFrame();

			// Stops the watcher thread. Called from
			// KarstExtension::OnProcessDetach().
			void Shutdown();

			// True when the feature parsed a usable list and the watcher is up.
			bool IsActive();
		}
	}
}
