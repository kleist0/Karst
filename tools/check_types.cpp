// Prints what Karst's INI hot reload makes of a given Types= value.
//
// Diagnostic companion to check_targets: when a Types= case comes out wrong,
// this shows the raw key and the resulting summary, which separates "the config
// was not read" from "the config was read and interpreted wrongly".
//
// Build with tools\build_tools.bat, run from anywhere:
//     check_types.exe <game directory>

#include <Ext/IniHotReload/IniReloadTarget.h>

#include <Config/INIConfigReader.h>

#include <cstdio>
#include <string>

static std::wstring Join(const std::wstring& directory, const wchar_t* name)
{
	std::wstring result = directory;
	if (!result.empty() && result.back() != L'\\')
		result.push_back(L'\\');
	result += name;
	return result;
}

int main(int argc, char** argv)
{
	const char* gameDirectory = (argc > 1) ? argv[1] : ".\\";

	wchar_t wide[1024] = { 0 };
	::MultiByteToWideChar(CP_ACP, 0, gameDirectory, -1, wide, 1024);

	std::wstring directory = wide;
	if (!directory.empty() && directory.back() != L'\\')
		directory.push_back(L'\\');

	const std::wstring configPath = Join(directory, L"karst.hotreload.ini");

	// Raw view first: what does the INI reader actually see?
	{
		Karst::INIConfigReader reader;
		const bool loaded = reader.LoadFile(configPath.c_str());

		printf("config file : %ls\n", configPath.c_str());
		printf("LoadFile    : %s\n", loaded ? "ok" : "FAILED");
		printf("HasSection  : %s\n",
			reader.HasSection("IniHotReload") ? "yes" : "no");
		printf("Enabled raw : '%s'\n",
			reader.GetString("IniHotReload", "Enabled", "<absent>").c_str());
		printf("Files   raw : '%s'\n",
			reader.GetString("IniHotReload", "Files", "<absent>").c_str());
		printf("Types   raw : '%s'\n\n",
			reader.GetString("IniHotReload", "Types", "<absent>").c_str());
	}

	const bool ok = Karst::Ext::IniHotReload::Load(directory.c_str());

	printf("Load()      : %s\n", ok ? "ok" : "FAILED");
	printf("summary     : %s\n\n", Karst::Ext::IniHotReload::TypeKindSummary());

	struct Probe { const char* Name; unsigned Kind; };
	const Probe probes[] = {
		{ "BuildingType",       7  },
		{ "UnitType",           40 },
		{ "InfantryType",       16 },
		{ "AircraftType",       3  },
		{ "WeaponType",         49 },
		{ "WarheadType",        50 },
		{ "AnimType",           5  },
		{ "IsotileType",        18 },
		{ "TerrainType",        37 },
	};

	for (const Probe& probe : probes)
	{
		printf("  %-16s -> %s\n", probe.Name,
			Karst::Ext::IniHotReload::ShouldReloadType(probe.Kind) ? "refeed" : "-");
	}

	return ok ? 0 : 1;
}
