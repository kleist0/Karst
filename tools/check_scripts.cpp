// Checks the script loader: where scripts are found, what is accepted, and what
// is refused - all without the game.
//
// The loader is the part of the scripting system that decides whether a user's
// script is honoured or rejected, so it has to be right in both directions: a
// script that should work but does not is a bug report, and a malformed script
// that is accepted silently is worse, because it does something the author did
// not ask for.
//
// Build with tools\build_tools.bat and run it from anywhere:
//     check_scripts.exe

#include <Script/ScriptLoader.h>
#include <Script/Json.h>

#include <io.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace Karst::Script;

static int Failures = 0;

static void Check(const char* what, bool condition)
{
	printf("  %-56s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++Failures;
}

// --- capturing the loader's log -------------------------------------------
//
// The loader writes its diagnostics through printf, which is exactly what makes
// it testable outside the game: redirect stdout to a temporary file, run the
// loader, then read the file back. "Was this rejected?" is then a fact about the
// output rather than an assumption.
//
// The redirection is done with the CRT's own _dup/_dup2 rather than freopen: a
// freopen only replaces the stream's destination, and the standard output handle
// keeps pointing at the console, which made half the output escape the capture.

static std::string g_captured;
static int         g_savedStdout = -1;
static const wchar_t* const CaptureFile = L"script_loader_capture.txt";

static bool CaptureBegin()
{
	fflush(stdout);

	g_savedStdout = _dup(_fileno(stdout));
	if (g_savedStdout == -1)
		return false;

	FILE* capture = nullptr;
	if (_wfopen_s(&capture, CaptureFile, L"w") != 0 || !capture)
		return false;

	// Redirect the descriptor, not just this FILE*.
	if (_dup2(_fileno(capture), _fileno(stdout)) != 0)
	{
		fclose(capture);
		return false;
	}

	fclose(capture);
	return true;
}

static void CaptureEnd()
{
	fflush(stdout);

	if (g_savedStdout != -1)
	{
		_dup2(g_savedStdout, _fileno(stdout));
		_close(g_savedStdout);
		g_savedStdout = -1;
	}

	std::string text;
	if (ReadTextFile(CaptureFile, text))
		g_captured = text;
	else
		g_captured.clear();

	::DeleteFileW(CaptureFile);
}

static bool LogHas(const char* needle)
{
	return g_captured.find(needle) != std::string::npos;
}

// --- fixtures --------------------------------------------------------------

static void WriteText(const std::wstring& path, const std::string& text)
{
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (file == INVALID_HANDLE_VALUE)
		return;

	DWORD written = 0;
	::WriteFile(file, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
	::CloseHandle(file);
}

struct Fixture
{
	std::wstring Root;

	bool Create()
	{
		wchar_t here[MAX_PATH] = { 0 };
		::GetModuleFileNameW(nullptr, here, MAX_PATH);

		Root = here;
		const size_t slash = Root.find_last_of(L"\\/");
		Root = (slash == std::wstring::npos) ? std::wstring() : Root.substr(0, slash + 1);

		static unsigned counter = 0;
		++counter;

		Root += L"script_fixture_";
		Root += std::to_wstring(counter);

		if (!::CreateDirectoryW(Root.c_str(), nullptr) &&
			::GetLastError() != ERROR_ALREADY_EXISTS)
			return false;

		Root.push_back(L'\\');

		return ::CreateDirectoryW((Root + L"scripts").c_str(), nullptr) != FALSE ||
			::GetLastError() == ERROR_ALREADY_EXISTS;
	}

	void WriteConfig(const char* text)
	{
		WriteText(Root + L"karst.scripts.ini", text);
	}

	void WriteScript(const wchar_t* name, const std::string& text)
	{
		WriteText(Root + L"scripts\\" + name, text);
	}

	void Remove()
	{
		WIN32_FIND_DATAW found = { 0 };
		HANDLE search = ::FindFirstFileW((Root + L"scripts\\*").c_str(), &found);

		if (search != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					::DeleteFileW((Root + L"scripts\\" + found.cFileName).c_str());
			} while (::FindNextFileW(search, &found));

			::FindClose(search);
		}

		::RemoveDirectoryW((Root + L"scripts").c_str());
		::DeleteFileW((Root + L"karst.scripts.ini").c_str());
		::RemoveDirectoryW(Root.c_str());
	}
};

static const char* const GoodConfig =
	"[Scripts]\r\nEnabled=yes\r\nDirectory=scripts\r\n";

int main()
{
	printf("script loader checks\n\n");

	// --- config handling ---------------------------------------------------
	{
		printf("config:\n");

		{
			Fixture fx;
			fx.Create();
			ScriptConfig config;
			Check("no config file -> scripts off",
				!LoadScriptConfig(fx.Root.c_str(), config) && !config.Found);
			fx.Remove();
		}

		{
			Fixture fx;
			fx.Create();
			fx.WriteConfig("[Scripts]\r\nEnabled=no\r\nDirectory=scripts\r\n");
			ScriptConfig config;
			Check("Enabled=no -> scripts off",
				!LoadScriptConfig(fx.Root.c_str(), config) && !config.Enabled);
			fx.Remove();
		}

		{
			Fixture fx;
			fx.Create();
			fx.WriteConfig(GoodConfig);
			ScriptConfig config;
			const bool ok = LoadScriptConfig(fx.Root.c_str(), config);
			Check("valid config accepted", ok && config.Enabled);
			Check("directory resolved", !config.Directory.empty());
			fx.Remove();
		}

		{
			Fixture fx;
			fx.Create();
			fx.WriteConfig("[Scripts]\r\nEnabled=yes\r\nDirectory=does_not_exist\r\n");
			ScriptConfig config;
			Check("missing directory -> scripts off",
				!LoadScriptConfig(fx.Root.c_str(), config) && !config.Enabled);
			fx.Remove();
		}
	}

	// --- good scripts ------------------------------------------------------
	{
		printf("\naccepted scripts:\n");

		Fixture fx;
		fx.Create();
		fx.WriteConfig(GoodConfig);

		fx.WriteScript(L"appearance.jsonc",
			"{\n"
			"  // a per-country look and a single look\n"
			"  \"scripts\": [\n"
			"    {\n"
			"      \"id\": \"allied-power-look\",\n"
			"      \"name\": \"Allied power plant per country\",\n"
			"      \"type\": \"appearance\",\n"
			"      \"building\": \"GAPOWR\",\n"
			"      \"byCountry\": { \"Americans\": \"YAPOWR\", \"Alliance\": \"NAPOWR\" },\n"
			"    },\n"
			"    {\n"
			"      \"type\": \"appearance\",\n"
			"      \"building\": \"AMCV\",\n"
			"      \"look\": \"SMCV\",\n"
			"      \"foundationFrom\": \"SMCV\",\n"
			"    },\n"
			"  ],\n"
			"}\n");

		std::vector<AppearanceRule> rules;
		ScriptConfig config;
		LoadScriptConfig(fx.Root.c_str(), config);

		CaptureBegin();
		const size_t count = LoadAppearanceRules(config, rules);
		CaptureEnd();

		Check("two rules loaded", count == 2 && rules.size() == 2);

		const AppearanceRule* power = FindAppearanceRule(rules, "GAPOWR");
		Check("per-country rule found", power != nullptr);
		Check("  its building is recorded",
			power && power->Building == "GAPOWR");
		Check("  it has two countries", power && power->ByCountry.size() == 2);
		Check("  it has no single look", power && power->Look.empty());
		Check("  US look resolves",
			power && std::string(LookForCountry(*power, "Americans")) == "YAPOWR");
		Check("  KR look resolves",
			power && std::string(LookForCountry(*power, "Alliance")) == "NAPOWR");
		Check("  unlisted country resolves to nothing",
			power && LookForCountry(*power, "French") == nullptr);
		Check("  lookup is case-insensitive",
			power && std::string(LookForCountry(*power, "americans")) == "YAPOWR");

		const AppearanceRule* mcv = FindAppearanceRule(rules, "amcv");
		Check("single-look rule found (case-insensitive)", mcv != nullptr);
		Check("  its look is recorded", mcv && mcv->Look == "SMCV");
		Check("  its foundation source is recorded",
			mcv && mcv->FoundationFrom == "SMCV");
		Check("  any country gets the same look",
			mcv && std::string(LookForCountry(*mcv, "French")) == "SMCV");

		Check("an unknown building has no rule",
			FindAppearanceRule(rules, "NOPE") == nullptr);

		fx.Remove();
	}

	// --- rejected scripts --------------------------------------------------
	{
		printf("\nrejected scripts:\n");

		struct BadCase
		{
			const char*  Name;
			const char*  Text;
			const char*  ExpectInLog;
			size_t       ExpectAccepted;
		};

		const BadCase cases[] = {
			{
				"malformed.jsonc",
				"{ \"type\": \"appearance\", \"building\": }\n",
				"expected a value",
				0
			},
			{
				"wrongtype.jsonc",
				"{ \"type\": \"sparkles\", \"building\": \"GAPOWR\", \"look\": \"X\" }\n",
				"unknown script type",
				0
			},
			{
				"nobuilding.jsonc",
				"{ \"type\": \"appearance\", \"look\": \"YAPOWR\" }\n",
				"needs 'building'",
				0
			},
			{
				"notype.jsonc",
				"{ \"building\": \"GAPOWR\", \"look\": \"YAPOWR\" }\n",
				"missing 'type'",
				0
			},
			{
				"nolook.jsonc",
				"{ \"type\": \"appearance\", \"building\": \"GAPOWR\" }\n",
				"needs either 'look'",
				0
			},
			{
				"emptycountry.jsonc",
				"{ \"type\": \"appearance\", \"building\": \"GAPOWR\",\n"
				"  \"byCountry\": { \"Americans\": \"\", \"Alliance\": \"NAPOWR\" } }\n",
				"needs a non-empty section name",
				1
			},
		};

		for (const BadCase& item : cases)
		{
			Fixture fx;
			fx.Create();
			fx.WriteConfig(GoodConfig);
			fx.WriteScript(L"bad.jsonc", item.Text);

			std::vector<AppearanceRule> rules;
			ScriptConfig config;
			LoadScriptConfig(fx.Root.c_str(), config);

			CaptureBegin();
			LoadAppearanceRules(config, rules);
			CaptureEnd();

			char what[160] = { 0 };
			_snprintf_s(what, sizeof(what), _TRUNCATE, "%s: rejected with a reason",
				item.Name);
			Check(what, LogHas(item.ExpectInLog));

			_snprintf_s(what, sizeof(what), _TRUNCATE, "%s: %u rule(s) accepted",
				item.Name, static_cast<unsigned>(item.ExpectAccepted));
			Check(what, rules.size() == item.ExpectAccepted);

			fx.Remove();
		}
	}

	// --- several files, one broken -----------------------------------------
	{
		printf("\nseveral files:\n");

		Fixture fx;
		fx.Create();
		fx.WriteConfig(GoodConfig);
		fx.WriteScript(L"a.jsonc",
			"{ \"type\": \"appearance\", \"building\": \"GAPOWR\", \"look\": \"YAPOWR\" }\n");
		fx.WriteScript(L"b.jsonc", "{ this is not json }\n");
		fx.WriteScript(L"c.jsonc",
			"{ \"type\": \"appearance\", \"building\": \"GAPILE\", \"look\": \"YABRCK\" }\n");

		std::vector<AppearanceRule> rules;
		ScriptConfig config;
		LoadScriptConfig(fx.Root.c_str(), config);

		CaptureBegin();
		LoadAppearanceRules(config, rules);
		CaptureEnd();

		Check("the two good files still load", rules.size() == 2);
		Check("the broken file is named in the log", LogHas("b.jsonc"));
		Check("good file a is present", FindAppearanceRule(rules, "GAPOWR") != nullptr);
		Check("good file c is present", FindAppearanceRule(rules, "GAPILE") != nullptr);

		fx.Remove();
	}

	// --- the shipped example must stay valid --------------------------------
	//
	// The example under scripts\examples is documentation, and documentation that
	// no longer parses is worse than none: it teaches the wrong shape. This case
	// loads the real file, so breaking it breaks the build.
	{
		printf("\nshipped example:\n");

		// Resolve next to the executable, not relative to the working directory:
		// the tool is run from wherever the caller happens to be.
		wchar_t herePath[MAX_PATH] = { 0 };
		::GetModuleFileNameW(nullptr, herePath, MAX_PATH);

		std::wstring hereDirectory = herePath;
		const size_t hereSlash = hereDirectory.find_last_of(L"\\/");
		hereDirectory = (hereSlash == std::wstring::npos)
			? std::wstring() : hereDirectory.substr(0, hereSlash + 1);

		// Path of the example, built from the tool's own directory: the tool is
		// run from wherever the caller happens to be, so relative paths are not
		// trustworthy here. (Note the wording of this comment: a line ending in a
		// backslash is a line continuation, and the compiler happily comments out
		// the line after it.)
		std::wstring examplePathW = hereDirectory + L"..\\scripts\\examples\\";

		wchar_t exampleResolved[MAX_PATH] = { 0 };
		Check("example directory resolves",
			_wfullpath(exampleResolved, examplePathW.c_str(), MAX_PATH) != nullptr);

		std::wstring exampleDirectory = exampleResolved;
		if (!exampleDirectory.empty() && exampleDirectory.back() != L'\\')
			exampleDirectory.push_back(L'\\');

		const std::wstring exampleFile =
			exampleDirectory + L"appearance.jsonc";

		Check("example file is reachable",
			::GetFileAttributesW(exampleFile.c_str()) != INVALID_FILE_ATTRIBUTES);

		if (::GetFileAttributesW(exampleFile.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			std::string text;
			Check("example file is readable",
				ReadTextFile(exampleFile.c_str(), text));

			JsonValue document;
			std::string error;

			const bool parsed = ParseJson(text, document, error);
			Check("example parses as JSON", parsed);

			if (!parsed)
				printf("     %s\n", error.c_str());

			const size_t count = document.Member("scripts").AsArray().size();
			Check("example declares three appearance scripts", count == 3);

			// And the loader agrees, given a config pointing at it.
			ScriptConfig config;
			config.Enabled = true;
			config.Directory = exampleDirectory;

			std::vector<AppearanceRule> rules;

			CaptureBegin();
			const size_t loaded = LoadAppearanceRules(config, rules);
			CaptureEnd();

			Check("loader accepts all three", loaded == 3);
			Check("  power plant rule present",
				FindAppearanceRule(rules, "GAPOWR") != nullptr);
			Check("  construction yard rule present",
				FindAppearanceRule(rules, "GACNST") != nullptr);
			Check("  barracks rule present",
				FindAppearanceRule(rules, "GAPILE") != nullptr);

			const AppearanceRule* power = FindAppearanceRule(rules, "GAPOWR");
			Check("  power plant has the two countries",
				power && power->ByCountry.size() == 2);
		}
	}

	// --- foundationFrom: "self" means "look only" ---------------------------
	{
		printf("\nfoundation control:\n");

		Fixture fx;
		fx.Create();
		fx.WriteConfig(GoodConfig);
		fx.WriteScript(L"foundation.jsonc",
			"{ \"scripts\": [\n"
			"  { \"type\": \"appearance\", \"building\": \"GAPILE\", \"look\": \"YABRCK\",\n"
			"    \"foundationFrom\": \"self\" },\n"
			"  { \"type\": \"appearance\", \"building\": \"GAPOWR\", \"look\": \"YAPOWR\",\n"
			"    \"foundationFrom\": \"YAPOWR\" },\n"
			"  { \"type\": \"appearance\", \"building\": \"GACNST\", \"look\": \"YACNST\" }\n"
			"] }\n");

		std::vector<AppearanceRule> rules;
		ScriptConfig config;
		LoadScriptConfig(fx.Root.c_str(), config);

		CaptureBegin();
		LoadAppearanceRules(config, rules);
		CaptureEnd();

		const AppearanceRule* barracks = FindAppearanceRule(rules, "GAPILE");
		Check("'self' is accepted", barracks != nullptr);
		Check("  it marks the footprint as kept",
			barracks && barracks->KeepFoundation);
		Check("  and does not name a foundation source",
			barracks && barracks->FoundationFrom.empty());
		Check("  but still carries the look",
			barracks && barracks->Look == "YABRCK");

		const AppearanceRule* power = FindAppearanceRule(rules, "GAPOWR");
		Check("an explicit source is not mistaken for 'self'",
			power && !power->KeepFoundation && power->FoundationFrom == "YAPOWR");

		const AppearanceRule* yard = FindAppearanceRule(rules, "GACNST");
		Check("omitting it makes the footprint follow the look",
			yard && !yard->KeepFoundation && yard->FoundationFrom == "YACNST");

		fx.Remove();
	}

	// --- a foundationFrom that disagrees with look --------------------------
	{
		printf("\nmismatched footprint:\n");

		Fixture fx;
		fx.Create();
		fx.WriteConfig(GoodConfig);
		fx.WriteScript(L"mismatch.jsonc",
			"{ \"type\": \"appearance\", \"building\": \"GAPILE\", "
			"\"look\": \"NAHAND\", \"foundationFrom\": \"YABRCK\" }\n");

		std::vector<AppearanceRule> mismatchRules;
		ScriptConfig mismatchConfig;
		LoadScriptConfig(fx.Root.c_str(), mismatchConfig);

		CaptureBegin();
		LoadAppearanceRules(mismatchConfig, mismatchRules);
		CaptureEnd();

		Check("the mismatch is reported",
			LogHas("differs from 'look'"));
		Check("the explicit value is still honoured",
			FindAppearanceRule(mismatchRules, "GAPILE") != nullptr);

		fx.Remove();
	}

	// --- hot reload change detection ----------------------------------------
	{
		printf("\nhot reload:\n");

		Fixture fx;
		fx.Root = L"";   // assigned below

		// A fixture whose *root* holds the config and whose scripts live one level
		// down, matching how the game lays it out.
		wchar_t here[MAX_PATH] = { 0 };
		::GetModuleFileNameW(nullptr, here, MAX_PATH);

		std::wstring root = here;
		const size_t slash = root.find_last_of(L"\\/");
		root = (slash == std::wstring::npos) ? std::wstring() : root.substr(0, slash + 1);

		static unsigned counter2 = 0;
		++counter2;
		root += L"hotreload_fixture_";
		root += std::to_wstring(counter2);
		root += L"\\";

		::CreateDirectoryW(root.c_str(), nullptr);
		::CreateDirectoryW((root + L"scripts").c_str(), nullptr);

		// HotReload=yes: reload-on-change is opt-in (off by default), so a test
		// that checks detection has to ask for it.
		WriteText(root + L"karst.scripts.ini",
			"[Scripts]\r\nEnabled=yes\r\nDirectory=scripts\r\nHotReload=yes\r\n");

		Karst::Script::SetLogSink(nullptr);

		CaptureBegin();
		Karst::Script::Init(root.c_str());
		CaptureEnd();

		Check("init loads the (empty) script set",
			Karst::Script::GetAppearanceRules().empty());

		const unsigned long long stampAtInit = Karst::Script::CurrentScriptStamp();
		Check("a baseline timestamp exists", stampAtInit != 0);

		// Save a script that is OLDER than the baseline, so any later write is
		// unambiguously newer. Without this the test would depend on the
		// filesystem clock advancing between two calls a millisecond apart.
		const std::wstring scriptPath = root + L"scripts\\live.jsonc";
		{
			HANDLE file = ::CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

			if (file != INVALID_HANDLE_VALUE)
			{
				const char* body =
					"{ \"type\": \"appearance\", \"building\": \"GAPILE\", "
					"\"look\": \"YABRCK\" }\n";

				DWORD written = 0;
				::WriteFile(file, body, static_cast<DWORD>(::lstrlenA(body)),
					&written, nullptr);

				// Backdate by 60 seconds using an ABSOLUTE time: passing a
				// negative FILETIME asks for a relative time, which does not land
				// here (measured: the file kept its creation time).
				FILETIME now = { 0 };
				::GetSystemTimeAsFileTime(&now);

				ULARGE_INTEGER value;
				value.HighPart = now.dwHighDateTime;
				value.LowPart = now.dwLowDateTime;
				value.QuadPart -= 600000000ULL;   // 60 s in 100 ns units

				FILETIME older;
				older.dwHighDateTime = value.HighPart;
				older.dwLowDateTime = value.LowPart;

				::SetFileTime(file, nullptr, nullptr, &older);

				::CloseHandle(file);
			}
		}

		Check("a file older than the baseline is NOT a change",
			Karst::Script::CurrentScriptStamp() <= stampAtInit);

		if (Karst::Script::CurrentScriptStamp() > stampAtInit)
		{
			// Diagnose: is the backdating not landing, or is the reader wrong?
			WIN32_FILE_ATTRIBUTE_DATA info = { 0 };
			::GetFileAttributesExW(scriptPath.c_str(), GetFileExInfoStandard, &info);
			const unsigned long long direct =
				(static_cast<unsigned long long>(info.ftLastWriteTime.dwHighDateTime) << 32)
				| info.ftLastWriteTime.dwLowDateTime;
			printf("     stampAtInit=%llu now=%llu fileDirect=%llu\n",
				stampAtInit, Karst::Script::CurrentScriptStamp(), direct);
		}

		// Now save it again, this time with a current timestamp.
		{
			HANDLE file = ::CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

			if (file != INVALID_HANDLE_VALUE)
			{
				// A VALID appearance script: the loader refuses one without a
				// look, so an invalid body here would make this test measure the
				// rejection path instead of the reload path.
				const char* body =
					"{ \"type\": \"appearance\", \"building\": \"GAPILE\", "
					"\"look\": \"YABRCK\" }\n";
				DWORD written = 0;
				::WriteFile(file, body, static_cast<DWORD>(::lstrlenA(body)),
					&written, nullptr);

				// Give the file an explicit, CURRENT timestamp.
				//
				// Relying on the filesystem to stamp the write is what made this
				// test flaky: after the backdated write above, an immediate
				// rewrite often kept the SAME timestamp (the file system caches
				// the clock), so "did the change get noticed" could not be
				// answered. Setting the time explicitly makes the test say what
				// it means.
				FILETIME now = { 0 };
				::GetSystemTimeAsFileTime(&now);
				::SetFileTime(file, nullptr, nullptr, &now);

				::CloseHandle(file);
			}
		}

		// Let the filesystem settle before asking whether the save was noticed.
		//
		// A save and the check can otherwise land within the same timestamp tick,
		// and the assertion then fails for a reason that has nothing to do with
		// the code under test. In the game this never matters: the check runs on
		// every frame, so a tick of delay is invisible.
		::Sleep(50);

		const unsigned long long stampAfterSave = Karst::Script::CurrentScriptStamp();
		Check("saving a script moves the timestamp",
			stampAfterSave > stampAtInit);

		if (stampAfterSave <= stampAtInit)
		{
			WIN32_FILE_ATTRIBUTE_DATA info = { 0 };
			::GetFileAttributesExW(scriptPath.c_str(), GetFileExInfoStandard, &info);
			unsigned long long direct =
				(static_cast<unsigned long long>(info.ftLastWriteTime.dwHighDateTime) << 32)
				| info.ftLastWriteTime.dwLowDateTime;
			printf("     atInit=%llu afterSave=%llu fileNow=%llu\n",
				stampAtInit, stampAfterSave, direct);
		}

		// And OnFrame picks it up.
		bool reloaded = false;
		for (int i = 0; i < 3 && !reloaded; ++i)
			reloaded = Karst::Script::OnFrame();

		Check("OnFrame reports the reload", reloaded);
		Check("the new rule is active",
			FindAppearanceRule(Karst::Script::GetAppearanceRules(), "GAPILE") != nullptr);
		Check("a second OnFrame is quiet (no repeat reload)",
			!Karst::Script::OnFrame());

		// Clean up.
		::DeleteFileW(scriptPath.c_str());
		::DeleteFileW((root + L"karst.scripts.ini").c_str());
		::RemoveDirectoryW((root + L"scripts").c_str());
		::RemoveDirectoryW(root.c_str());
	}

	printf("\n%s (%d failure(s))\n",
		Failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", Failures);

	return Failures == 0 ? 0 : 1;
}
