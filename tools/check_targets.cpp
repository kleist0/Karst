// Lists what Karst's INI hot reload would actually watch, without the game.
//
// The list is the whole boundary of the feature (a file that is not on it is
// never reread), so it is worth being able to check it on its own: one run here
// shows every accepted entry, and proves that matching says "no" for everything
// that is not on the list.
//
// It links the real IniReloadTarget.obj, so it tests the shipped code and not a
// copy of it. Build (32-bit):
//
//     cl /nologo /EHsc /I..\src /I..\YRpp /Fe:check_targets.exe check_targets.cpp ^
//        ..\src\Ext\IniHotReload\IniReloadTarget.cpp ..\src\Config\INIConfigReader.cpp ^
//        ..\src\Config\KarstConfig.cpp ..\src\Core\Log.cpp
//
// Run from the folder that holds karst.hotreload.ini - normally the game
// directory - and pass the game directory as the first argument.

#include <Ext/IniHotReload/IniReloadTarget.h>

#include <Config/INIConfigReader.h>

#include <cstdio>
#include <string>

namespace
{
	int Failures = 0;

	void Check(const char* what, bool condition)
	{
		printf("  %-46s %s\n", what, condition ? "ok" : "FAILED");
		if (!condition)
			++Failures;
	}

	// Builds "<gameDirectory>\<name>", the shape the watcher reports.
	// Tolerates a directory that already ends with a separator, so callers can
	// pass either form (a doubled backslash is not a valid path to CreateFile).
	std::wstring Join(const std::wstring& directory, const wchar_t* name)
	{
		std::wstring result = directory;
		while (!result.empty() && (result.back() == L'\\' || result.back() == L'/'))
			result.pop_back();
		result.push_back(L'\\');
		result += name;
		return result;
	}

	// Writes a throwaway game directory holding a config with the given
	// Types= value plus two real .ini files, loads it, and reports which type
	// kinds came out enabled.
	struct KindCase
	{
		const char*      Types;
		unsigned         Kind;         // AbstractType value
		const char*      KindName;
		bool             Expected;
	};

	void RunTypeKindCase(const KindCase& item)
	{
		// A UNIQUE directory per case.
		//
		// Three traps got this wrong before it worked: RemoveDirectory only
		// removes an EMPTY directory, so a shared directory kept the previous
		// case's files; GetTickCount64 barely advances between cases in a tight
		// loop, so "unique" names collided and every case read the first case's
		// config; and %TEMP% is not reliably writable in every environment
		// (CreateFile there failed, and the cases then ran against a config that
		// was never written). The tool's own directory plus a pid and a counter
		// is unique and writable.
		static unsigned counter = 0;
		++counter;

		wchar_t here[MAX_PATH] = { 0 };
		::GetModuleFileNameW(nullptr, here, MAX_PATH);

		std::wstring directory = here;
		const size_t slash = directory.find_last_of(L"\\/");
		directory = (slash == std::wstring::npos)
			? std::wstring() : directory.substr(0, slash + 1);

		wchar_t unique[96] = { 0 };
		::swprintf_s(unique, L"kind_fixture_%lu_%u",
			static_cast<unsigned long>(::GetCurrentProcessId()), counter);

		directory += unique;

		::CreateDirectoryW(directory.c_str(), nullptr);
		directory.push_back(L'\\');

		// The config the loader is pointed at explicitly, so the fixture can live
		// anywhere instead of next to the executable.
		const std::wstring configPath = Join(directory, L"karst.hotreload.ini");

		const std::string config =
			std::string("[IniHotReload]\r\nEnabled=yes\r\n"
				"Files=rulesmd.ini,artmd.ini\r\nTypes=") + item.Types + "\r\n";

		HANDLE file = ::CreateFileW(Join(directory, L"karst.hotreload.ini").c_str(),
			GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE)
		{
			DWORD written = 0;
			::WriteFile(file, config.c_str(), static_cast<DWORD>(config.size()),
				&written, nullptr);
			::CloseHandle(file);
		}

		const char* empty = "[General]\r\nSpeed=1\r\n";
		for (const wchar_t* name : { L"rulesmd.ini", L"artmd.ini" })
		{
			HANDLE f = ::CreateFileW(Join(directory, name).c_str(), GENERIC_WRITE, 0,
				nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f != INVALID_HANDLE_VALUE)
			{
				DWORD written = 0;
				::WriteFile(f, empty, 17, &written, nullptr);
				::CloseHandle(f);
			}
		}

		// The override is what makes this testable: without it the feature looks
		// for the list file in the directory of the running executable, which is
		// this tool's own output directory, not the fixture.
		Karst::Ext::IniHotReload::Load(directory.c_str(), configPath.c_str());
		const bool actual =
			Karst::Ext::IniHotReload::ShouldReloadType(item.Kind);

		char what[128] = { 0 };
		_snprintf_s(what, sizeof(what), _TRUNCATE, "Types=%-24s %-18s -> %s",
			item.Types, item.KindName, actual ? "yes" : "no");

		const bool pass = (actual == item.Expected);
		Check(what, pass);

		if (!pass)
		{
			// Show what the load actually saw - a failure here is almost always
			// "the config was not the one I thought I wrote".
			char narrow[512] = { 0 };
			::WideCharToMultiByte(CP_ACP, 0, configPath.c_str(), -1, narrow, 512,
				nullptr, nullptr);

			Karst::INIConfigReader verify;
			const bool readBack = verify.LoadFile(configPath.c_str());

			printf("         config=%s summary='%s'\n", narrow,
				Karst::Ext::IniHotReload::TypeKindSummary());
			printf("         wrote Types='%s' read-back=%s saw Types='%s'\n",
				item.Types, readBack ? "ok" : "FAILED",
				readBack
					? verify.GetString("IniHotReload", "Types", "<absent>").c_str()
					: "<unreadable>");
		}

		::DeleteFileW(Join(directory, L"karst.hotreload.ini").c_str());
		::DeleteFileW(Join(directory, L"rulesmd.ini").c_str());
		::DeleteFileW(Join(directory, L"artmd.ini").c_str());
		::RemoveDirectoryW(directory.c_str());
	}
}

int main(int argc, char** argv)
{
	const char* gameDirectory = (argc > 1) ? argv[1] : ".\\";

	wchar_t wide[1024] = { 0 };
	::MultiByteToWideChar(CP_ACP, 0, gameDirectory, -1, wide, 1024);

	std::wstring directory = wide;
	if (!directory.empty() && directory.back() != L'\\')
		directory.push_back(L'\\');

	printf("game directory : %ls\n", directory.c_str());
	printf("list file      : %s\n\n", Karst::Ext::IniHotReload::TargetListFileName());

	// --- what the config itself says (raw, before any interpretation) -------
	{
		Karst::INIConfigReader reader;
		if (reader.LoadFile(Join(directory, L"karst.hotreload.ini").c_str()))
		{
			printf("config Enabled : '%s'\n",
				reader.GetString("IniHotReload", "Enabled", "<absent>").c_str());
			printf("config Files   : '%s'\n\n",
				reader.GetString("IniHotReload", "Files", "<absent>").c_str());
		}
		else
		{
			printf("config         : karst.hotreload.ini not readable\n\n");
		}
	}

	const bool ok = Karst::Ext::IniHotReload::Load(directory.c_str());

	printf("feature        : %s\n", ok ? "ON" : "OFF");
	printf("watched files  : %u\n", static_cast<unsigned>(Karst::Ext::IniHotReload::Count()));

	for (size_t i = 0; i < Karst::Ext::IniHotReload::Count(); ++i)
	{
		const Karst::Ext::IniHotReload::Target& target =
			Karst::Ext::IniHotReload::Get(i);

		printf("  [%u] %-24s -> %ls\n", static_cast<unsigned>(i), target.Name,
			target.FullPath.c_str());
	}

	// --- matching: the list is the boundary, so "no" matters as much as "yes"
	printf("\nmatching:\n");

	struct Probe
	{
		const wchar_t* Name;
		bool           Expected;
	};

	const Probe probes[] = {
		{ L"rulesmd.ini",  true  },
		{ L"RULESMD.INI",  true  },   // case-insensitive
		{ L"artmd.ini",    true  },
		{ L"aimd.ini",     true  },
		{ L"RA2MD.INI",    false },   // the game's own settings file
		{ L"karst.ini",    false },   // Karst's own config
		{ L"ddraw.ini",    false },
		{ L"soundmd.ini",  false },
		{ L"rules.txt",    false },
	};

	for (const Probe& probe : probes)
	{
		char narrow[64] = { 0 };
		::WideCharToMultiByte(CP_ACP, 0, probe.Name, -1, narrow, 64, nullptr, nullptr);

		const bool matched =
			Karst::Ext::IniHotReload::Matches(Join(directory, probe.Name));

		printf("  %-14s %-8s %s\n", narrow,
			matched ? "WATCHED" : "-",
			matched == probe.Expected ? "" : "  <-- UNEXPECTED");

		if (matched != probe.Expected)
			++Failures;
	}

	printf("\nassertions:\n");
	Check("feature is ON", ok);
	Check("at least one file watched",
		Karst::Ext::IniHotReload::Count() > 0);
	Check("a file outside the list is not watched",
		!Karst::Ext::IniHotReload::Matches(Join(directory, L"soundmd.ini")));

	// --- Types= : which kinds may be refed --------------------------------
	printf("\ntype kinds (Types=):\n");

	const KindCase kinds[] = {
		{ "default",    7,  "BuildingType",    true  },
		{ "default",    40, "UnitType",        false },
		{ "default",    49, "WeaponType",      false },

		{ "all",        7,  "BuildingType",    true  },
		{ "all",        40, "UnitType",        true  },
		{ "all",        49, "WeaponType",      true  },
		{ "all",        5,  "AnimType",        true  },
		{ "all",        37, "TerrainType",     false },   // refused outright

		{ "none",       7,  "BuildingType",    false },

		{ "",           7,  "BuildingType",    true  },   // absent -> default
		{ "nonsense",   7,  "BuildingType",    true  },   // unknown -> default
		{ "terraintype",37, "TerrainType",     false },

		{ "buildings,units", 7,  "BuildingType", true  },
		{ "buildings,units", 16, "InfantryType", true  },
		// The "units" group covers unit/infantry/aircraft. Note the value: 3 is
		// AircraftType - 2 is Aircraft, an instance type that has no type object
		// and is therefore never in this table.
		{ "buildings,units", 3,  "AircraftType", true  },

		// A group name ADDS to the default; it does not replace it. Only "all",
		// "none" and "default" reset the set, and they act left to right. So
		// "weapons" is buildings + weapons, which is the case pinned here because
		// it is the one that surprises people (see karst.hotreload.ini).
		{ "weapons",    49, "WeaponType",      true  },
		{ "weapons",    7,  "BuildingType",    true  },

		{ "units",      40, "UnitType",        true  },
		{ "units",      7,  "BuildingType",    true  },   // additive, same rule

		{ "WarheadType", 50, "WarheadType",    true  },
		{ "WarheadType", 49, "WeaponType",     false },
		{ "WarheadType", 7,  "BuildingType",   true  },   // still additive

		// Starting from "none" is the documented way to ask for an EXACT set.
		// Without these cases nothing pins that escape hatch, and a staged
		// rollout would have no way to isolate one kind.
		{ "none,weapons", 49, "WeaponType",    true  },
		{ "none,weapons", 7,  "BuildingType",  false },
		{ "none,units",   40, "UnitType",      true  },
		{ "none,units",   16, "InfantryType",  true  },
		{ "none,units",   7,  "BuildingType",  false },
		{ "none",         49, "WeaponType",    false },

		// The staged-rollout form: verified baseline plus one new group.
		{ "default,units", 7,  "BuildingType", true  },
		{ "default,units", 40, "UnitType",     true  },
	};

	for (const KindCase& item : kinds)
		RunTypeKindCase(item);

	printf("\n%s (%d failure(s))\n", Failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
		Failures);

	return Failures == 0 ? 0 : 1;
}
