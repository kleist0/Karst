#include <Karst.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Ext/IniHotReload/IniHotReload.h>
#include <Ext/ShowByCountry/ShowByCountry.h>
#include <Script/ScriptLoader.h>
#include <Script/ClrHost.h>
#include <Version.h>

#include <string>
#include <vector>

#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <ColorScheme.h>
#include <HouseClass.h>
#include <MessageListClass.h>

HMODULE KarstExtension::ModuleHandle = nullptr;
bool KarstExtension::Attached = false;
bool KarstExtension::GameStarted = false;

namespace
{
	// Sends the script loader's diagnostics to karst.log. Without this they would
	// only reach the debugger, which is not where a user looks.
	void ScriptLogSink(const char* message)
	{
		Karst::LogInfo(L"Scripts: %hs", message);
	}

	// Loads the scripts for the first time. Runs on the game thread from
	// OnGameStart, i.e. after the loader lock is gone, so file I/O here is safe.
	//
	// The rules themselves live in the script module (Karst::Script), which also
	// owns change detection - see ReloadScriptsIfChanged.
	void InitScripts()
	{
		wchar_t path[MAX_PATH] = { 0 };
		if (::GetModuleFileNameW(nullptr, path, _countof(path)) == 0)
			return;

		std::wstring directory(path);
		const size_t slash = directory.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return;

		directory = directory.substr(0, slash + 1);

		Karst::Script::SetLogSink(&ScriptLogSink);
		Karst::Script::Init(directory.c_str());

		// The C# script system: start the .NET runtime and load the three
		// assemblies in <game>\KarstPatcher\. Done here rather than in DllMain
		// because starting the CLR under the loader lock is how a process
		// deadlocks itself.
		//
		// Both steps only log and return false on failure - a machine without
		// the assemblies keeps every other Karst feature.
		if (Karst::Script::InitClr(directory.c_str()))
			Karst::Script::LoadScriptSystem();
	}
}

Karst::Config& KarstExtension::GetConfig()
{
	// Function-local static: constructed on first use, so there is no static
	// initialisation-order problem with other translation units.
	static Karst::Config instance;
	return instance;
}

const std::vector<Karst::Script::AppearanceRule>&
KarstExtension::GetScriptAppearanceRules()
{
	// Owned by the script module, which replaces the list when a script changes.
	return Karst::Script::GetAppearanceRules();
}

bool KarstExtension::ScriptsEnabled()
{
	return Karst::Script::ScriptsAreEnabled();
}

bool KarstExtension::ReloadScriptsIfChanged()
{
	if (!Karst::Script::OnFrame())
		return false;

	// The rules changed: tell the modules that act on them to pick the new ones
	// up. Both calls are idempotent and only write when a value actually
	// differs, so calling them again is safe.
	//
	// What takes effect immediately: the ART redirection and the footprint, i.e.
	// what the ENGINE does when it parses a type's art. What does NOT: the
	// per-country type copies CountryArt builds, because those are made during
	// that same parse. A new copy for an already-parsed type needs a new match -
	// otherwise a building already on the map would keep pointing at the old
	// copy.
	Karst::Ext::ShowByCountry::ParseSectionRedirects();
	Karst::Ext::ShowByCountry::ApplyFoundationFromSection("script reload");

	Karst::LogInfo(L"Scripts: %u appearance rule(s) active.",
		static_cast<unsigned>(Karst::Script::GetAppearanceRules().size()));
	Karst::LogInfo(L"Scripts: reloaded - new look takes effect on the next ART "
		L"parse (a newly built or newly placed building, or the next match).");

	return true;
}

void KarstExtension::OnProcessAttach(HMODULE module)
{
	KarstExtension::ModuleHandle = module;
	KarstExtension::Attached = true;

	// At this point the loader lock is held: only touch memory and primitives.
	// File I/O and the logger are set up in OnGameStart().
}

void KarstExtension::OnProcessDetach()
{
	if (!KarstExtension::Attached)
		return;

	// Drop per-building state before the engine goes away, so nothing points at
	// a BuildingClass that is about to be freed.
	Karst::Ext::ShowByCountry::ResetForNewMatch();

	// Stop the INI watcher thread before the process tears down.
	Karst::Ext::IniHotReload::Shutdown();

	Karst::Log::Shutdown();

	KarstExtension::Attached = false;
	KarstExtension::GameStarted = false;
	KarstExtension::ModuleHandle = nullptr;
}

void KarstExtension::OnGameStart()
{
	if (KarstExtension::GameStarted)
		return;

	KarstExtension::GameStarted = true;

	KarstExtension::ApplyConfig();

	Karst::LogInfo(L"%s started (build %s).", KARST_NAME_WSTR, KARST_VERSION_FULL_WSTR);

	// A SEPARATE feature (Ext\IniHotReload). Reads its own list file and starts a
	// watcher thread; it makes no engine call until a watched INI actually
	// changes, and adds no hook of its own - the reload rides on OnFrame below.
	Karst::Ext::IniHotReload::Init();

	// User scripts. Loaded here so any problem with them is reported before the
	// game gets far; acting on the rules happens later, at the points where the
	// modules already decide a building's art.
	InitScripts();

	// Register your own features here, e.g.
	//   Ext::MyFeature::Init();
	// Each feature should read its own keys from KarstExtension::GetConfig().
	//
	// See src\Ext\README.md for the intended layout.
	//
	// Note: features that need the engine's INI data cannot run yet. This hook
	// fires at the executable entry point, before the type lists and ART INI are
	// loaded. Such features retry from OnFrame instead - see ShowByCountry there.
}

void KarstExtension::ApplyConfig()
{
	Karst::Config& config = KarstExtension::GetConfig();
	const bool enabled = config.Load();

	Karst::Log::Init(config);

	if (!config.Loaded())
	{
		Karst::LogInfo(L"karst.ini not found, using defaults.");
	}
	else
	{
		Karst::LogInfo(L"Loaded %ls", config.Path().c_str());
	}

	if (!enabled)
	{
		// Enabled=no. Keep the DLL resident but do nothing: every feature is
		// gated behind this flag, and the log level has already been applied so
		// the reason stays visible in the debug output.
		Karst::LogInfo(L"Karst is disabled by karst.ini (Enabled=no).");
	}

	Karst::LogDebug(L"Config: Enabled=%d LogLevel=%hs DebugLog=%d ShowActiveMessage=%d IniValidationDialog=%d",
		config.Enabled() ? 1 : 0,
		Karst::Config::LogLevelName(config.GetLogLevel()),
		config.DebugLog() ? 1 : 0,
		config.ShowActiveMessage() ? 1 : 0,
		config.ShowIniValidationDialog() ? 1 : 0);
}

DWORD __cdecl KarstExtension::OnEntryPointHook(void* /*registers*/)
{
	// The hook fires on the game's main thread, so this is the safe moment to
	// touch the file system, the CRT heap and the logger.
	KarstExtension::OnGameStart();

	// 0 means "continue": Syringe restores the original instructions and the
	// game proceeds normally. Return a non-zero value only if you intend to
	// replace the hooked code entirely.
	return 0;
}

void KarstExtension::OnFrame()
{
	// Runs every frame from the main loop hook in dllmain.cpp.
	//
	// This skeleton deliberately does NOT touch any engine object here.
	//
	// The reason is worth knowing before you add one: YRpp globals such as
	// MessageListClass::Instance are bound to fixed addresses, and "the address
	// is readable" is NOT the same as "the pointer is a live engine object".
	// Outside gamemd.exe - or before the engine publishes a global - that fixed
	// address can hold unrelated mapped memory, so VirtualQuery says readable,
	// the pointer survives validation, and the first virtual call through it
	// crashes. Karst::MemorySafety filters the obvious cases; it cannot make a
	// stale pointer safe.
	//
	// So a message posted from here would be a real crash risk. Keep per-frame
	// work limited to the DLL's own state, or gate engine calls behind a signal
	// the engine itself sets, e.g. the player object appearing in
	// HouseClass::Array (read via MemorySafety) or a scenario flag.
	//
	// When you are ready to post the banner, this is the shape to use - note
	// that it belongs behind a real "engine is up" signal, not merely a
	// readability check:
	//
	//     MessageListClass* messageList =
	//         Karst::MemorySafety::SafeValidate(MessageListClass::Instance);
	//     if (messageList)
	//         messageList->PrintMessage(L"[" KARST_NAME_WSTR L"]", 0,
	//             KARST_ACTIVE_MESSAGE, ColorScheme::Yellow, true);
	//
	static bool announced = false;
	if (!announced)
	{
		announced = true;

		const Karst::Config& config = KarstExtension::GetConfig();
		Karst::LogDebug(L"First frame reached (main loop hook is live, enabled=%d).",
			config.Enabled() ? 1 : 0);
	}

	// Deferred configuration parsing.
	//
	// ShowByCountries/ShowBySHP read the engine's ART INI and its building type
	// Deferred readiness check.
	//
	// ShowByArt reads the engine's rules INI, which does not exist at the entry
	// point. Retry here until the feature reports that the engine is ready.
	if (!Karst::Ext::ShowByCountry::IsInitialized() &&
		KarstExtension::GetConfig().Enabled())
	{
		Karst::Ext::ShowByCountry::Init();
	}

	if (Karst::Ext::ShowByCountry::IsInitialized())
	{
		// Marks the feature active and applies the footprint copy once. The ART
		// redirect itself happens earlier, in the engine's ART parser hook - that
		// is the only moment at which changing a type's section name can still
		// influence what gets parsed.
		//
		// There is no per-frame sweep any more. The sweep existed to walk every
		// building and apply a per-country rule to the ones whose owner matched;
		// with the per-country feature removed, nothing needs to be examined per
		// building, so no frame time is spent on it.
		Karst::Ext::ShowByCountry::MarkGameRunning();
	}

	// A SEPARATE feature (Ext\IniHotReload). Does nothing at all - one atomic
	// read - until a file listed in karst.hotreload.ini is saved while the game
	// runs; then it refeeds the engine's type objects from the new INI data.
	// This frame boundary IS the feature's synchronisation point, which is why
	// it needs no hook of its own.
	Karst::Ext::IniHotReload::OnFrame();

	// User scripts: re-read them when one of them changed. Costs one directory
	// scan per frame, and does nothing else until a timestamp moves.
	KarstExtension::ReloadScriptsIfChanged();

	// C# scripts: drive their per-frame callback.
	//
	// The reference sources have NO caller for OnUpdate (verified by searching
	// both assemblies), so scripts compiled, loaded and were attached to units
	// yet never ran. This frame boundary is the driver: it calls the managed
	// Program.DoTick(), which raises the event ScriptManager subscribes to.
	//
	// No-op until the script system is loaded; managed exceptions are caught on
	// the managed side so a faulting script cannot reach the engine loop.
	Karst::Script::TickScripts();
}
