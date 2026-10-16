#pragma once

// ===========================================================================
//  ClrHost - starts the .NET runtime and loads the script system
// ===========================================================================
//
//  WHY THIS EXISTS
//
//  User scripts are C#. Running them means hosting the .NET Framework runtime
//  inside the game process, then loading the three assemblies built from
//  refs\karstpatcher (see that folder's README):
//
//      KarstPatcher.dll    the runtime: hooks, Roslyn compile, script loading
//      KarstYRpp.dll       the bindings: Pointer<T>, TechnoClass, MapClass, ...
//      KarstExtension.dll  TechnoExt/BulletExt, TechnoScriptable, decorators
//
//  The namespaces inside those assemblies are unchanged (KarstPatcher,
//  PatcherYRpp, Extension) so the reference project's scripts still compile;
//  only the FILE names carry the Karst prefix, so both projects can be
//  installed side by side without clashing.
//
//  WHEN IT RUNS
//
//  From KarstExtension::OnGameStart - never from DllMain. Starting the CLR
//  under the loader lock is the classic way to deadlock a process, and the
//  reference project avoids it too (it defers to a hook that runs after the
//  window exists).
//
//  WHAT NOTHING HERE DEPENDS ON
//
//  If the assemblies are missing, or the runtime will not start, every entry
//  point returns false having logged why. The rest of Karst is unaffected -
//  the INI hot reload and the appearance features keep working.

namespace Karst
{
	namespace Script
	{
		// Starts the .NET runtime pointed at `karstpatcher` (v4.0.30319).
		//
		// `gameDirectory` must end with a backslash: the assemblies are expected
		// in <gameDirectory>karstpatcher\.
		//
		// Safe to call more than once; only the first call does work.
		// Returns false when the runtime could not be started.
		bool InitClr(const wchar_t* gameDirectory);

		// Loads KarstPatcher.dll and calls Program.Activate().
		//
		// Separate from InitClr so the two failure modes are distinguishable in
		// the log: "the runtime would not start" and "the runtime started but the
		// assembly would not load" need different fixes.
		bool LoadScriptSystem();

		// True once LoadScriptSystem() has succeeded.
		bool ScriptSystemLoaded();

		// Drives every live script for one frame.
		//
		// WHY KARST DRIVES IT
		//
		// The script's per-frame callback has no caller in the reference sources:
		// neither the host nor the extension ever invokes OnUpdate (verified by
		// searching both assemblies). Scripts therefore compiled, loaded and were
		// attached, yet never ran. Karst already has a per-frame point
		// (KarstExtension::OnFrame), so the driver is wired there: this calls the
		// managed Program.DoTick(), which raises the Tick event that
		// ScriptManager subscribes to.
		//
		// Called every frame. Does nothing until the script system is loaded, and
		// swallows managed exceptions (a script fault must not reach the engine's
		// main loop).
		void TickScripts();
	}
}
