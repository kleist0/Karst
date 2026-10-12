#include <Script/ScriptLoader.h>

#include <Config/INIConfigReader.h>
#include <Script/Json.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Karst
{
	namespace Script
	{
		namespace
		{
			const char* const ScriptsConfigName = "karst.scripts.ini";
			const char* const ScriptsSection = "Scripts";

			// The game directory, with a trailing backslash: the Directory=
			// setting in karst.scripts.ini is relative to it.
			std::wstring GameDirectory;

			// The loaded rules, owned here so a reload is visible to every
			// consumer without any cross-module callback.
			std::vector<AppearanceRule> AppearanceRules;
			bool ScriptsEnabled = false;

			// The newest script file timestamp as of the last load. A file with a
			// later timestamp means "reload". Zero means "nothing loaded yet".
			unsigned long long LastScriptChange = 0;

			// Optional sink for diagnostics; the game installs one that writes to
			// karst.log. Null means "debugger + stdout".
			LogSink Sink = nullptr;

			// Diagnostics. Scripts are loaded before the game has a console, so
			// OutputDebugString is the reliable default; the same text also goes
			// to stdout, which is what the standalone check tool reads.
			void Report(const char* fmt, ...)
			{
				char message[1024] = { 0 };

				va_list args;
				va_start(args, fmt);
				_vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, args);
				va_end(args);

				if (Sink)
				{
					Sink(message);
					return;
				}

				::OutputDebugStringA("[Karst/Scripts] ");
				::OutputDebugStringA(message);
				::OutputDebugStringA("\n");

				printf("[Karst/Scripts] %s\n", message);
			}

			bool EqualsIgnoreCase(const std::string& left, const char* right)
			{
				return right && ::_stricmp(left.c_str(), right) == 0;
			}

			std::string Trim(const std::string& value)
			{
				size_t begin = 0;
				size_t end = value.size();

				while (begin < end && (value[begin] == ' ' || value[begin] == '\t' ||
					value[begin] == '\r' || value[begin] == '\n'))
					++begin;
				while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
					value[end - 1] == '\r' || value[end - 1] == '\n'))
					--end;

				return value.substr(begin, end - begin);
			}

			std::wstring Widen(const std::string& text)
			{
				if (text.empty())
					return std::wstring();

				const int needed = ::MultiByteToWideChar(
					CP_ACP, 0, text.c_str(), -1, nullptr, 0);

				if (needed <= 1)
					return std::wstring();

				std::wstring result(static_cast<size_t>(needed - 1), L'\0');
				::MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &result[0], needed);
				return result;
			}

			std::string Narrow(const std::wstring& text)
			{
				if (text.empty())
					return std::string();

				const int needed = ::WideCharToMultiByte(
					CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);

				if (needed <= 1)
					return std::string();

				std::string result(static_cast<size_t>(needed - 1), '\0');
				::WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, &result[0],
					needed, nullptr, nullptr);
				return result;
			}

			std::wstring JoinPath(const std::wstring& directory, const std::wstring& name)
			{
				std::wstring result = directory;
				while (!result.empty() &&
					(result.back() == L'\\' || result.back() == L'/'))
					result.pop_back();
				result.push_back(L'\\');
				result += name;
				return result;
			}

			bool HasJsoncSuffix(const std::wstring& name)
			{
				if (name.size() < 6)
					return false;

				const std::wstring tail = name.substr(name.size() - 6);
				return ::_wcsicmp(tail.c_str(), L".jsonc") == 0;
			}

			// "file:line" for a message about a value inside `file`.
			std::string Where(const std::string& file, const JsonValue& value)
			{
				char buffer[512] = { 0 };
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s:%d",
					file.c_str(), value.GetLine());
				return buffer;
			}

			// A string member, trimmed. Returns fallback when absent or not a
			// string, so a mistyped value behaves as "missing" rather than as
			// something surprising.
			std::string StringMember(const JsonValue& object, const char* name,
				const char* fallback = "")
			{
				const JsonValue& value = object.Member(name);
				return value.IsString() ? Trim(value.AsString()) : std::string(fallback);
			}

			bool AddAppearanceRule(const JsonValue& script, const std::string& where,
				const std::string& building, const std::string& id,
				std::vector<AppearanceRule>& out)
			{
				AppearanceRule rule;
				rule.Building = building;
				rule.Source = where;
				(void)id;

				const JsonValue& byCountry = script.Member("byCountry");
				const JsonValue& look = script.Member("look");

				if (byCountry.IsObject() && !byCountry.AsObject().empty())
				{
					if (look.IsString())
					{
						Report("%s: 'look' and 'byCountry' are mutually exclusive; "
							"'look' ignored.", where.c_str());
					}

					for (const auto& entry : byCountry.AsObject())
					{
						const std::string country = Trim(entry.first);
						const std::string section = entry.second.IsString()
							? Trim(entry.second.AsString()) : std::string();

						if (country.empty() || section.empty())
						{
							Report("%s: byCountry entry '%s' needs a non-empty "
								"section name; entry ignored.",
								where.c_str(), country.c_str());
							continue;
						}

						rule.ByCountry.push_back({ country, section });
					}

					if (rule.ByCountry.empty())
					{
						Report("%s: 'byCountry' has no usable entry; script ignored.",
							where.c_str());
						return false;
					}
				}
				else if (look.IsString() && !Trim(look.AsString()).empty())
				{
					rule.Look = Trim(look.AsString());
				}
				else
				{
					Report("%s: an appearance script needs either 'look' (a section "
						"name) or 'byCountry' (country -> section); script ignored.",
						where.c_str());
					return false;
				}

				rule.FoundationFrom = StringMember(script, "foundationFrom");

				// "foundationFrom": "self" (also accepted: none, keep) means the
				// look changes but the footprint does not. The INI form cannot
				// express this, which is why it is a script-only setting.
				if (!rule.FoundationFrom.empty() &&
					(EqualsIgnoreCase(rule.FoundationFrom, "self") ||
					 EqualsIgnoreCase(rule.FoundationFrom, "none") ||
					 EqualsIgnoreCase(rule.FoundationFrom, "keep")))
				{
					rule.KeepFoundation = true;
					rule.FoundationFrom.clear();
				}

				// Where the footprint comes from.
				//
				// DEFAULT: the same section as the look. The footprint has to
				// follow the ART, not be chosen independently - the model is drawn
				// for the target's Foundation, so leaving the original footprint in
				// place would put a 2x3 model on a 2x2 patch of ground. Follow it
				// unless the author says otherwise.
				if (rule.FoundationFrom.empty() && !rule.KeepFoundation &&
					!rule.Look.empty())
				{
					rule.FoundationFrom = rule.Look;
				}

				if (!rule.FoundationFrom.empty() && !rule.Look.empty() &&
					::_stricmp(rule.FoundationFrom.c_str(), rule.Look.c_str()) != 0)
				{
					// Legitimate (e.g. the weather controller, whose ART section
					// and type ID differ), but easy to do by accident when the look
					// is edited and this line is not.
					Report("%s: 'foundationFrom' (%s) differs from 'look' (%s) - the "
						"footprint will follow %s while the art comes from %s.",
						where.c_str(), rule.FoundationFrom.c_str(), rule.Look.c_str(),
						rule.FoundationFrom.c_str(), rule.Look.c_str());
				}

				if (!rule.FoundationFrom.empty() && rule.Look.empty() &&
					rule.ByCountry.size() == 1)
				{
					// Harmless, but worth saying: with per-country looks the
					// footprint has to come from somewhere explicit.
					Report("%s: 'foundationFrom' is given for a single-country rule.",
						where.c_str());
				}

				out.push_back(std::move(rule));
				return true;
			}

			void ValidateScript(const JsonValue& script, const std::string& file,
				std::vector<AppearanceRule>& out, size_t& added)
			{
				if (!script.IsObject())
				{
					Report("%s: a script must be an object; ignored.", file.c_str());
					return;
				}

				const std::string where = Where(file, script);

				const std::string id = StringMember(script, "id");
				const std::string type = StringMember(script, "type");
				const std::string building = StringMember(script, "building");

				if (type.empty())
				{
					Report("%s: missing 'type' (expected \"appearance\"); script ignored.",
						where.c_str());
					return;
				}

				if (!EqualsIgnoreCase(type, "appearance"))
				{
					Report("%s: unknown script type '%s'; ignored. (This build "
						"understands \"appearance\".)", where.c_str(), type.c_str());
					return;
				}

				if (building.empty())
				{
					Report("%s: an appearance script needs 'building'; script ignored.",
						where.c_str());
					return;
				}

				if (AddAppearanceRule(script, where, building, id, out))
					++added;
			}

			// One parsed file: either a single script or {"scripts": [...]}.
			size_t HandleDocument(const JsonValue& document, const std::string& file,
				std::vector<AppearanceRule>& out)
			{
				size_t added = 0;

				if (document.IsObject() && document.HasMember("scripts"))
				{
					const JsonValue& list = document.Member("scripts");

					if (!list.IsArray())
					{
						Report("%s:%d: 'scripts' must be an array; file ignored.",
							file.c_str(), list.GetLine());
						return 0;
					}

					for (const JsonValue& script : list.AsArray())
						ValidateScript(script, file, out, added);

					return added;
				}

				// A file with a single script object.
				ValidateScript(document, file, out, added);
				return added;
			}
		}

		const char* ConfigFileName()
		{
			return ScriptsConfigName;
		}

		void SetLogSink(LogSink sink)
		{
			Sink = sink;
		}

		// Hot reload lives at the END of this file, after LoadScriptConfig, so
		// its helpers can call the public entry point directly.

		bool LoadScriptConfig(const wchar_t* gameDirectory, ScriptConfig& out)
		{
			out = ScriptConfig();

			if (!gameDirectory || !*gameDirectory)
			{
				Report("no game directory; scripts off.");
				return false;
			}

			GameDirectory = gameDirectory;
			if (GameDirectory.back() != L'\\' && GameDirectory.back() != L'/')
				GameDirectory.push_back(L'\\');

			const std::wstring configPath =
				JoinPath(GameDirectory, Widen(ScriptsConfigName));

			const DWORD attributes = ::GetFileAttributesW(configPath.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES)
			{
				// Not an error: scripts are opt-in.
				Report("%s not found; scripts off.", Narrow(configPath).c_str());
				return false;
			}

			out.Found = true;

			INIConfigReader reader;
			if (!reader.LoadFile(configPath.c_str()))
			{
				Report("%s could not be read; scripts off.",
					Narrow(configPath).c_str());
				return false;
			}

			if (!reader.HasSection(ScriptsSection))
			{
				Report("%s has no [%s] section; scripts off.",
					Narrow(configPath).c_str(), ScriptsSection);
				return false;
			}

			if (!reader.GetBool(ScriptsSection, "Enabled", false))
			{
				Report("%s: Enabled=no; scripts off.", Narrow(configPath).c_str());
				return false;
			}

			const std::string directory = reader.GetString(ScriptsSection,
				"Directory", "");
			if (directory.empty())
			{
				Report("%s: Enabled=yes but Directory= is empty; scripts off.",
					Narrow(configPath).c_str());
				return false;
			}

			out.DirectorySetting = Widen(directory);
			out.Directory = JoinPath(GameDirectory, out.DirectorySetting);

			const DWORD directoryAttributes =
				::GetFileAttributesW(out.Directory.c_str());

			if (directoryAttributes == INVALID_FILE_ATTRIBUTES ||
				(directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				Report("script directory '%s' does not exist; scripts off.",
					Narrow(out.Directory).c_str());
				return false;
			}

			out.Enabled = true;
			out.HotReload = reader.GetBool(ScriptsSection, "HotReload", false);

			if (out.HotReload)
			{
				Report("script hot reload is ON (HotReload=yes).");
			}

			return true;
		}

		size_t LoadAppearanceRules(const ScriptConfig& config,
			std::vector<AppearanceRule>& out)
		{
			if (!config.Enabled)
				return 0;

			const std::wstring pattern = JoinPath(config.Directory, L"*.jsonc");

			WIN32_FIND_DATAW found = { 0 };
			HANDLE search = ::FindFirstFileW(pattern.c_str(), &found);

			if (search == INVALID_HANDLE_VALUE)
			{
				Report("no *.jsonc under '%s'.", Narrow(config.Directory).c_str());
				return 0;
			}

			const size_t before = out.size();

			do
			{
				if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;

				const std::wstring fileName = found.cFileName;
				if (!HasJsoncSuffix(fileName))
					continue;

				const std::wstring path = JoinPath(config.Directory, fileName);
				const std::string displayName = Narrow(fileName);

				std::string text;
				if (!ReadTextFile(path.c_str(), text))
				{
					Report("%s: could not be read (empty or locked); skipped.",
						displayName.c_str());
					continue;
				}

				JsonValue document;
				std::string error;

				if (!ParseJson(text, document, error))
				{
					Report("%s: %s; file skipped.",
						displayName.c_str(), error.c_str());
					continue;
				}

				const size_t added = HandleDocument(document, displayName, out);
				Report("%s: %u script(s) accepted.",
					displayName.c_str(), static_cast<unsigned>(added));

			} while (::FindNextFileW(search, &found));

			::FindClose(search);

			return out.size() - before;
		}

		bool LoadAll(const wchar_t* gameDirectory, std::vector<AppearanceRule>& rules)
		{
			rules.clear();

			ScriptConfig config;
			if (!LoadScriptConfig(gameDirectory, config))
				return false;

			const size_t count = LoadAppearanceRules(config, rules);

			Report("%u appearance rule(s) loaded from '%s'.",
				static_cast<unsigned>(count), Narrow(config.Directory).c_str());

			return true;
		}

		const AppearanceRule* FindAppearanceRule(
			const std::vector<AppearanceRule>& rules, const char* buildingId)
		{
			if (!buildingId)
				return nullptr;

			for (const AppearanceRule& rule : rules)
			{
				if (EqualsIgnoreCase(rule.Building, buildingId))
					return &rule;
			}

			return nullptr;
		}

		const char* LookForCountry(const AppearanceRule& rule, const char* country)
		{
			if (!rule.Look.empty())
				return rule.Look.c_str();

			if (!country)
				return nullptr;

			for (const auto& entry : rule.ByCountry)
			{
				if (EqualsIgnoreCase(entry.first, country))
					return entry.second.c_str();
			}

			return nullptr;
		}

		// --- hot reload ------------------------------------------------------
		//
		// Placed here, after LoadScriptConfig, so the helpers below can call the
		// public entry point directly instead of through a forward declaration.

		namespace
		{
			// A FILETIME as one sortable 64-bit value.
			unsigned long long FileTimeValue(const FILETIME& time)
			{
				return (static_cast<unsigned long long>(time.dwHighDateTime) << 32)
					| time.dwLowDateTime;
			}

			// The newest modification time among the script files. Zero when there
			// are none.
			//
			// Only the NEWEST matters: if any script is newer than the last load,
			// the whole set is read again. Reloading per file would be bookkeeping
			// with no payoff - a handful of small JSON files is not worth it.
			unsigned long long NewestScriptStamp(const std::wstring& directory)
			{
				const std::wstring pattern = JoinPath(directory, L"*.jsonc");

				WIN32_FIND_DATAW found = { 0 };
				HANDLE search = ::FindFirstFileW(pattern.c_str(), &found);

				if (search == INVALID_HANDLE_VALUE)
					return 0;

				unsigned long long newest = 0;

				do
				{
					if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						continue;

					const unsigned long long stamp = FileTimeValue(found.ftLastWriteTime);
					if (stamp > newest)
						newest = stamp;

				} while (::FindNextFileW(search, &found));

				::FindClose(search);
				return newest;
			}

			// The config file's own timestamp: a change to karst.scripts.ini (a
			// different Directory=, or Enabled=) has to reload too.
			unsigned long long ConfigStamp(const std::wstring& gameDirectory)
			{
				const std::wstring configPath =
					JoinPath(gameDirectory, Widen(ScriptsConfigName));

				WIN32_FILE_ATTRIBUTE_DATA attributes = { 0 };
				if (!::GetFileAttributesExW(configPath.c_str(), GetFileExInfoStandard,
					&attributes))
					return 0;

				return FileTimeValue(attributes.ftLastWriteTime);
			}

			// Cached state for the per-frame check.
			//
			// The config has to be re-read to notice a Directory= change, but
			// doing that EVERY frame means opening and parsing a file 30+ times a
			// second to learn nothing. The directory is therefore cached, the
			// config file's timestamp is re-read at most once a second, and the
			// scripts' directory is scanned every frame (one FindFirstFile, and it
			// is what actually has to be responsive).
			std::wstring       CachedScriptDirectory;
			unsigned long long CachedConfigStamp = 0;
			unsigned long long LastConfigCheck = 0;

			// Mirrors ScriptConfig::HotReload of the last load. When false - the
			// default - OnFrame() returns immediately and none of the scanning
			// below ever runs.
			bool ScriptHotReloadOn = false;

			// How many *.jsonc files the last load saw. A differ means a file was
			// added or DELETED, which a timestamp cannot express: removing the
			// newest file leaves the remaining ones older than LastScriptChange,
			// so "newer than last time" never fires and the rules would keep
			// applying after the user deleted the script.
			int LastScriptCount = 0;

			const unsigned long long ConfigCheckIntervalMs = 1000;

			// How many *.jsonc files are in `directory` right now.
			int ScriptFileCount(const std::wstring& directory)
			{
				const std::wstring pattern = JoinPath(directory, L"*.jsonc");

				WIN32_FIND_DATAW found = { 0 };
				HANDLE search = ::FindFirstFileW(pattern.c_str(), &found);

				if (search == INVALID_HANDLE_VALUE)
					return 0;

				int count = 0;

				do
				{
					if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						++count;

				} while (::FindNextFileW(search, &found));

				::FindClose(search);
				return count;
			}
		}

		// The newest modification time among the config file and the scripts, as
		// of right now.
		//
		// Public because change detection is worth testing directly: a test that
		// saves a script and asserts this value moved proves the mechanism without
		// sleeping and hoping the filesystem clock advanced.
		unsigned long long CurrentScriptStamp()
		{
			unsigned long long newest = ConfigStamp(GameDirectory);

			ScriptConfig config;
			if (LoadScriptConfig(GameDirectory.c_str(), config))
			{
				const unsigned long long scripts = NewestScriptStamp(config.Directory);
				if (scripts > newest)
					newest = scripts;
			}

			return newest;
		}

		bool Reload()
		{
			ScriptConfig config;

			// Refresh the caches the per-frame check relies on. This is the only
			// place they are written, so a Directory= change is picked up here and
			// nowhere else.
			CachedConfigStamp = ConfigStamp(GameDirectory);
			LastConfigCheck = ::GetTickCount64();

			if (!LoadScriptConfig(GameDirectory.c_str(), config))
			{
				// Scripts are off (or the config became unusable). The previously
				// loaded rules are dropped: keeping them would mean acting on a
				// configuration the user just disabled.
				AppearanceRules.clear();
				ScriptsEnabled = false;
				CachedScriptDirectory.clear();

				LastScriptChange = CurrentScriptStamp();
				return false;
			}

			CachedScriptDirectory = config.Directory;
			ScriptHotReloadOn = config.HotReload;
			LastScriptCount = ScriptFileCount(config.Directory);

			AppearanceRules.clear();
			const size_t count = LoadAppearanceRules(config, AppearanceRules);

			ScriptsEnabled = true;
			LastScriptChange = CurrentScriptStamp();

			Report("%u appearance rule(s) loaded from '%s'.",
				static_cast<unsigned>(count), Narrow(config.Directory).c_str());

			return true;
		}

		void Init(const wchar_t* gameDirectory)
		{
			if (!gameDirectory || !*gameDirectory)
			{
				Report("no game directory; scripts off.");
				return;
			}

			GameDirectory = gameDirectory;
			if (GameDirectory.back() != L'\\' && GameDirectory.back() != L'/')
				GameDirectory.push_back(L'\\');

			Reload();
		}

		bool OnFrame()
		{
			if (GameDirectory.empty())
				return false;

			// Off unless karst.scripts.ini says HotReload=yes. Disabled, the whole
			// feature costs one bool test per frame.
			if (!ScriptHotReloadOn)
				return false;

			// An added or DELETED script cannot be seen in a timestamp: deleting
			// the newest file leaves the rest older than the last load, so the
			// comparison further down would never fire and a deleted script would
			// keep applying. The file count catches that.
			if (!CachedScriptDirectory.empty())
			{
				const int count = ScriptFileCount(CachedScriptDirectory);

				if (count != LastScriptCount)
				{
					Report("script file count changed (%d -> %d) - reloading.",
						LastScriptCount, count);
					return Reload();
				}
			}

			// The config file's timestamp is re-read at most once a second: it
			// only matters when the user edits karst.scripts.ini, and parsing it
			// every frame to learn nothing would be wasteful.
			const unsigned long long now = ::GetTickCount64();
			if (now - LastConfigCheck >= ConfigCheckIntervalMs)
			{
				LastConfigCheck = now;
				CachedConfigStamp = ConfigStamp(GameDirectory);
			}

			unsigned long long newest = CachedConfigStamp;

			if (!CachedScriptDirectory.empty())
			{
				const unsigned long long scripts =
					NewestScriptStamp(CachedScriptDirectory);
				if (scripts > newest)
					newest = scripts;
			}

			if (newest == 0 || newest <= LastScriptChange)
				return false;

			Report("script change detected - reloading.");
			return Reload();
		}

		const std::vector<AppearanceRule>& GetAppearanceRules()
		{
			return AppearanceRules;
		}

		bool ScriptsAreEnabled()
		{
			return ScriptsEnabled;
		}
	}
}
