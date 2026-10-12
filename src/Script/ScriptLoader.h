#pragma once

#include <Windows.h>

#include <string>
#include <vector>

// Scripts - user-authored JSON that tells Karst what to do, so that a feature
// can be configured without editing INI files or writing C++.
//
// WHAT THIS FILE COVERS (step 1: appearance)
//
// The first kind of script is declarative configuration: "make this building
// look like another one", with or without a per-country distinction. That is
// exactly what ShowByArt / ShowByCountries already do through rulesmd.ini - this
// loader gives the same capability a friendlier, validated, editor-friendly
// source.
//
// WHY THE RULE OUTPUT IS ENGINE-FREE
//
// The loader produces a plain rule table and touches nothing in the game. That
// is what makes it testable outside the game (tools\check_scripts.cpp) and what
// keeps the risky part - deciding which ART section a type uses - in the modules
// that already handle it and have already been verified on a live game.
//
// PRECEDENCE
//
// Scripts win over INI. A script entry is the explicit, newer statement of
// intent, and having one rule win means a conflict is diagnosable by reading the
// script alone. INI rules remain fully supported; the modules simply consult the
// script table first.
//
// FILE FORMAT
//
// <game directory>\karst.scripts.ini selects where scripts live:
//
//     [Scripts]
//     Enabled=yes
//     Directory=Karst\scripts
//
// Every *.jsonc in that directory is read. Each file holds either one script
// object or {"scripts": [ ... ]}:
//
//     {
//       "scripts": [
//         {
//           "id": "allied-power-look",
//           "name": "Allied power plant per country",
//           "type": "appearance",
//           "building": "GAPOWR",
//           "byCountry": { "Americans": "YAPOWR", "Alliance": "NAPOWR" }
//         },
//         {
//           "type": "appearance",
//           "building": "AMCV",
//           "look": "SMCV",
//           "foundationFrom": "SMCV"
//         }
//       ]
//     }
//
// A malformed script is reported with its file, line and reason, and is skipped.
// It never half-applies: a rejected entry contributes no rule at all.

namespace Karst
{
	namespace Script
	{
		// One building's appearance override, as the scripts describe it.
		struct AppearanceRule
		{
			// The building this rule is about (its type ID, e.g. "GAPOWR").
			std::string Building;

			// The ART section to draw instead, when the rule is not per-country.
			// Empty when ByCountry is used.
			std::string Look;

			// Per-country ART sections: country name -> ART section. Empty for a
			// single-look rule.
			std::vector<std::pair<std::string, std::string>> ByCountry;

			// Where the footprint comes from. Empty means "same as Look" when
			// Look is set, otherwise "no footprint change".
			std::string FoundationFrom;

			// The script asked for the LOOK ONLY: the ART section changes, the
			// building keeps its own Foundation ("foundationFrom": "self").
			//
			// Worth having because following the target's footprint is not always
			// wanted: two Foundations can cover the same area in a different shape
			// (3x2 vs 2x3), and a script whose only intent was a different picture
			// should not quietly change where the building may be placed.
			bool KeepFoundation = false;

			// Where the rule came from, for logs: "file line N".
			std::string Source;
		};

		// Where scripts come from, parsed from karst.scripts.ini.
		struct ScriptConfig
		{
			bool        Enabled = false;      // Enabled=yes in the ini
			bool        Found = false;        // the ini existed
			std::wstring Directory;           // absolute, with a trailing backslash
			std::wstring DirectorySetting;    // as written, for logging

			// Hot reload of the JSON script files, off by default.
			//
			// Deliberately opt-in: a script is a build input, and the decision
			// taken for this project is that changing one and restarting is fine.
			// Leaving it off also means the per-frame timestamp scan never runs.
			// Turn it on with `HotReload=yes` in karst.scripts.ini.
			//
			// (The C# side has its own watcher in the script host and is not
			// affected by this.)
			bool        HotReload = false;
		};

		// The name of the config file, looked up in the game directory.
		const char* ConfigFileName();

		// Where the loader's diagnostics go.
		//
		// By default they are written with OutputDebugString and printf, which is
		// what makes the loader testable outside the game. The game itself wants
		// them in karst.log instead, so it installs a sink here.
		typedef void (*LogSink)(const char* message);
		void SetLogSink(LogSink sink);

		// Reads karst.scripts.ini from `gameDirectory` (must end with a
		// backslash). Fills `out`; returns false when scripts are disabled or the
		// config is unusable, having logged why.
		bool LoadScriptConfig(const wchar_t* gameDirectory, ScriptConfig& out);

		// Reads every *.jsonc under `config.Directory` and appends the appearance
		// rules they declare to `out`. Returns how many were appended.
		//
		// Every problem (bad JSON, unknown type, missing field, duplicate
		// building, script that names an ART section for a building the game does
		// not have) is logged with file and line, and the offending script is
		// skipped.
		size_t LoadAppearanceRules(const ScriptConfig& config,
			std::vector<AppearanceRule>& out);

		// Loads the config and the rules in one step, which is what the game
		// calls on startup. Returns false when scripts are off.
		bool LoadAll(const wchar_t* gameDirectory,
			std::vector<AppearanceRule>& rules);

		// --- hot reload ------------------------------------------------------
		//
		// The modules that act on the rules read them through
		// GetAppearanceRules(), so those rules have to live here: reloading them
		// then means replacing this module's own list, and every consumer sees
		// the new one on its next read.
		//
		// Change detection is a timestamp scan rather than a second watcher
		// thread: the INI hot reload already owns a directory watcher with its own
		// global state, and giving the script directory a second one would double
		// the moving parts for no benefit. One FindFirstFile per frame is cheap,
		// and the work only happens when a file is actually newer than the last
		// load.
		//
		// Called once from KarstExtension::OnGameStart, after the log sink is
		// installed. Reads the scripts and records the baseline timestamp.
		void Init(const wchar_t* gameDirectory);

		// Called once per frame. Detects a changed script and reloads it.
		// Returns true when a reload happened this frame.
		bool OnFrame();

		// The rules from the most recent load. Empty when scripts are off.
		const std::vector<AppearanceRule>& GetAppearanceRules();

		// True when scripts were loaded (even if they declared nothing).
		bool ScriptsAreEnabled();

		// Reads the scripts again, replacing the rule list. Logged.
		bool Reload();

		// The newest modification time among the config file and the scripts, as
		// of right now. Exposed so a change-detection test can prove that saving
		// a script actually moves it - the alternative is sleeping in a test and
		// hoping the filesystem clock advanced.
		unsigned long long CurrentScriptStamp();

		// The rule for `buildingId`, or nullptr. Case-insensitive.
		const AppearanceRule* FindAppearanceRule(const std::vector<AppearanceRule>& rules,
			const char* buildingId);

		// The ART section `country` should use for `rule`, or nullptr when this
		// country is not listed (in which case the building keeps its own art,
		// matching how ShowByCountries behaves for an unlisted country).
		const char* LookForCountry(const AppearanceRule& rule, const char* country);
	}
}
