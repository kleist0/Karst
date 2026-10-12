#pragma once

#include <Windows.h>

#include <Config/KarstConfig.h>
#include <Script/ScriptLoader.h>

#include <vector>

// The extension's main object.
//
// Lifecycle: DllMain(PROCESS_ATTACH) -> KarstExtension::OnProcessAttach() reads
// karst.ini and starts the logger; the Syringe hook installed in dllmain.cpp
// then calls KarstExtension::OnGameStart() once gamemd.exe reaches its entry
// point. On PROCESS_DETACH, KarstExtension::OnProcessDetach() releases the
// logger.
//
// Add new features by creating a file under src\Ext and calling its Init() from
// KarstExtension::OnGameStart(). Files under src\Ext should stay
// dependency-free so they can be enabled or removed without touching the rest
// of the project.
//
// The class is deliberately not called `Karst`: the configuration and logging
// helpers live in a namespace of that name (namespace Karst), and a class and a
// namespace cannot share a name in the same scope.

class KarstExtension
{
public:
	// Process-wide state. Safe to touch from anywhere in the DLL.
	static Karst::Config& GetConfig();

	// The appearance rules the user's scripts declared, loaded once at start.
	//
	// Scripts take precedence over the equivalent INI settings: a module that
	// reads a building's art configuration consults this first, and only falls
	// back to the INI when the script says nothing about that building.
	//
	// The list lives in the script module, which owns it and replaces it when a
	// script changes - so a consumer that reads it again sees the new rules with
	// no notification needed.
	static const std::vector<Karst::Script::AppearanceRule>& GetScriptAppearanceRules();

	// True when scripts were enabled and their list was read (even if it declared
	// nothing). Lets a module tell "no script rules" from "scripts are off".
	static bool ScriptsEnabled();

	// Called once per frame from the main loop. Re-reads the scripts when one of
	// them changed, and asks the modules that act on the rules to pick them up.
	// Returns true when a reload happened.
	static bool ReloadScriptsIfChanged();

	// --- Lifecycle -------------------------------------------------------

	// Runs in DllMain. Never call loader-locking APIs such as LoadLibrary here.
	static void OnProcessAttach(HMODULE module);
	static void OnProcessDetach();

	// Runs from the Syringe hook, i.e. on the game's main thread once the
	// executable entry point is reached. This is the right place for any work
	// that touches engine classes, the file system, or the heap.
	static void OnGameStart();

	// --- Syringe entry point ---------------------------------------------

	// Handles the hook installed at gamemd.exe's entry point (0x7CD810).
	// The address and hook size live in the DEFINE_HOOK call in dllmain.cpp,
	// because the macro needs compile-time literals.
	static DWORD __cdecl OnEntryPointHook(void* registers);

	// Called once per frame by the main loop hook. Posts the "is active"
	// message on the first frame where the engine is ready for it.
	static void OnFrame();

private:
	// Loads karst.ini, starts the logger and reports what was found.
	// Called once from OnGameStart().
	static void ApplyConfig();

	static HMODULE ModuleHandle;
	static bool Attached;
	static bool GameStarted;
};
