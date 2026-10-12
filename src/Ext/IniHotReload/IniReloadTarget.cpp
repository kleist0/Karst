#include <Ext/IniHotReload/IniReloadTarget.h>

#include <Config/INIConfigReader.h>
#include <Core/Log.h>

#include <vector>

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			namespace
			{
				const char* const ListFileName = "karst.hotreload.ini";
				const char* const SectionName = "IniHotReload";
				const char* const FilesKey = "Files";
				const char* const TypesKey = "Types";

				// Table size used by AbstractClass::GetClassName for AbstractType.
				const size_t TypeKindCount = 74;

				// The suffix the feature accepts. Hard-coded on purpose: see the
				// header. A ".ini" check is what makes the configured array a list
				// of INI files and nothing else.
				const wchar_t* const AcceptedSuffix = L".ini";

				Target      TargetArray[MaxTargets];
				size_t      TargetCount = 0;
				bool        EnabledFlag = false;
				std::wstring GameDirectory;   // with a trailing backslash

				// Name -> type kind, for the configurable list. The names match
				// AbstractClass::GetClassName's, so the log and the distribution
				// report read the same way.
				struct TypeKindName
				{
					const char* Name;
					TypeKind    Kind;
				};

				const TypeKindName TypeKindNames[] = {
					{ "BuildingType",       TypeKind::BuildingType       },
					{ "UnitType",           TypeKind::UnitType           },
					{ "InfantryType",       TypeKind::InfantryType       },
					{ "AircraftType",       TypeKind::AircraftType       },
					{ "WeaponType",         TypeKind::WeaponType         },
					{ "WarheadType",        TypeKind::WarheadType        },
					{ "BulletType",         TypeKind::BulletType         },
					{ "AnimType",           TypeKind::AnimType           },
					{ "VoxelAnimType",      TypeKind::VoxelAnimType      },
					{ "ParticleType",       TypeKind::ParticleType       },
					{ "ParticleSystemType", TypeKind::ParticleSystemType },
					{ "OverlayType",        TypeKind::OverlayType        },
					{ "SmudgeType",         TypeKind::SmudgeType         },
					{ "IsotileType",        TypeKind::IsotileType        },
					{ "SuperWeaponType",    TypeKind::SuperWeaponType    },
					{ "HouseType",          TypeKind::HouseType          },
					{ "Side",               TypeKind::Side               },
					// TerrainType is deliberately absent: refeeding it was
					// measured as a crash by the reference implementation.
				};

				// Which type kinds may be refed. A flat table, so the check is one
				// indexing operation, and so "all except TerrainType" has
				// somewhere to live.
				bool        TypeKindEnabled[TypeKindCount] = { false };
				// KARST: 512, not 160. All 17 addressable kind names need about 190
				// characters, so the 160-byte buffer silently cut the list short:
				// measured with Types=all, the startup line stopped at "OverlayType"
				// and the last five kinds were invisible - in the one line whose whole
				// purpose is telling the user which kinds are live.
				char        TypeKindSummaryText[512] = { 0 };

				void ResetTypeKinds()
				{
					for (size_t i = 0; i < TypeKindCount; ++i)
						TypeKindEnabled[i] = false;
				}

				void SetTypeKind(TypeKind kind, bool on)
				{
					const size_t index = static_cast<size_t>(kind);

					// TerrainType is not in the table at all, which is what
					// refuses it even when it is named in the config.
					if (index < TypeKindCount)
						TypeKindEnabled[index] = on;
				}

				// "all" means every mapped kind.
				void SetAllMappedKinds(bool on)
				{
					for (const TypeKindName& entry : TypeKindNames)
						SetTypeKind(entry.Kind, on);
				}

				void SetDefaultKinds()
				{
					ResetTypeKinds();
					SetTypeKind(TypeKind::BuildingType, true);
				}

				std::string TrimInline(const std::string& value)
				{
					size_t begin = 0;
					size_t end = value.size();

					while (begin < end && (value[begin] == ' ' || value[begin] == '\t'))
						++begin;
					while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
						--end;

					return value.substr(begin, end - begin);
				}

				// Splits a comma separated list, trimming and dropping empties.
				void SplitList(const std::string& value, std::vector<std::string>& out)
				{
					size_t begin = 0;

					for (;;)
					{
						const size_t comma = value.find(',', begin);
						const size_t end = (comma == std::string::npos)
							? value.size() : comma;

						std::string item = TrimInline(value.substr(begin, end - begin));

						if (!item.empty())
							out.push_back(item);

						if (comma == std::string::npos)
							break;

						begin = comma + 1;
					}
				}

				// Builds the enabled table from the configured value, reporting
				// anything it does not recognise instead of guessing.
				void ParseTypeKinds(const std::string& value)
				{
					SetDefaultKinds();
					TypeKindSummaryText[0] = '\0';

					if (value.empty())
						return;

					std::vector<std::string> items;
					SplitList(value, items);

					bool first = true;
					bool sawAnything = false;

					for (const std::string& raw : items)
					{
						const std::string item = TrimInline(raw);

						if (item.empty())
							continue;

						if (::_stricmp(item.c_str(), "default") == 0)
						{
							SetDefaultKinds();
							sawAnything = true;
						}
						else if (::_stricmp(item.c_str(), "all") == 0)
						{
							ResetTypeKinds();
							SetAllMappedKinds(true);
							sawAnything = true;
						}
						else if (::_stricmp(item.c_str(), "none") == 0)
						{
							ResetTypeKinds();
							sawAnything = true;
						}
						else if (::_stricmp(item.c_str(), "buildings") == 0)
						{
							SetDefaultKinds();
							sawAnything = true;
						}
						else if (::_stricmp(item.c_str(), "units") == 0)
						{
							SetTypeKind(TypeKind::UnitType, true);
							SetTypeKind(TypeKind::InfantryType, true);
							SetTypeKind(TypeKind::AircraftType, true);
							sawAnything = true;
						}
						else if (::_stricmp(item.c_str(), "weapons") == 0)
						{
							SetTypeKind(TypeKind::WeaponType, true);
							SetTypeKind(TypeKind::WarheadType, true);
							SetTypeKind(TypeKind::BulletType, true);
							sawAnything = true;
						}
						else if (::_stricmp(item.c_str(), "terraintype") == 0)
						{
							LogWarn(L"IniHotReload: '%hs' in %hs is refused - "
								L"refeeding TerrainType was measured as a crash.",
								item.c_str(), TypesKey);
						}
						else
						{
							bool known = false;

							for (const TypeKindName& entry : TypeKindNames)
							{
								if (::_stricmp(item.c_str(), entry.Name) == 0)
								{
									SetTypeKind(entry.Kind, true);
									known = true;
									sawAnything = true;
									break;
								}
							}

							if (!known)
							{
								LogWarn(L"IniHotReload: '%hs' in %hs is not a known "
									L"type kind and was ignored.", item.c_str(), TypesKey);
							}
						}

						first = false;
					}

					if (!sawAnything)
						SetDefaultKinds();
				}

				// "BuildingType, WeaponType" or "none", for the startup log.
				void BuildTypeKindSummary()
				{
					TypeKindSummaryText[0] = '\0';
					const size_t capacity = sizeof(TypeKindSummaryText);
					size_t written = 0;
					unsigned count = 0;
					bool truncated = false;

					for (const TypeKindName& entry : TypeKindNames)
					{
						const size_t index = static_cast<size_t>(entry.Kind);
						if (index >= TypeKindCount || !TypeKindEnabled[index])
							continue;

						++count;

						const size_t entryLength = ::strlen(entry.Name);
						const size_t separator = (written > 0) ? 2 : 0;

						// Room for the separator, the name, and the terminator.
						if (written + separator + entryLength + 1 > capacity)
						{
							truncated = true;
							break;
						}

						if (written > 0)
						{
							::strncat_s(TypeKindSummaryText, capacity, ", ", _TRUNCATE);
							written += 2;
						}

						::strncat_s(TypeKindSummaryText, capacity, entry.Name, _TRUNCATE);
						written += entryLength;
					}

					if (count == 0)
						::strncat_s(TypeKindSummaryText, capacity, "none", _TRUNCATE);

					// Never report a shortened list as if it were complete.
					if (truncated)
						::strncat_s(TypeKindSummaryText, capacity, ", ...", _TRUNCATE);
				}

				std::string Narrow(const wchar_t* text)
				{
					if (!text)
						return std::string();

					const int needed = ::WideCharToMultiByte(
						CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);

					if (needed <= 1)
						return std::string();

					std::string result(static_cast<size_t>(needed - 1), '\0');
					::WideCharToMultiByte(
						CP_ACP, 0, text, -1, &result[0], needed, nullptr, nullptr);

					return result;
				}

				std::wstring Widen(const char* text)
				{
					if (!text || !*text)
						return std::wstring();

					const int needed = ::MultiByteToWideChar(
						CP_ACP, 0, text, -1, nullptr, 0);

					if (needed <= 1)
						return std::wstring();

					std::wstring result(static_cast<size_t>(needed - 1), L'\0');
					::MultiByteToWideChar(
						CP_ACP, 0, text, -1, &result[0], needed);

					return result;
				}

				// Lower-case for case-insensitive comparison. Windows paths are
				// case-insensitive, so every comparison in this module folds case.
				std::wstring Lowercase(const std::wstring& text)
				{
					std::wstring result = text;
					for (wchar_t& c : result)
					{
						if (c >= L'A' && c <= L'Z')
							c = static_cast<wchar_t>(c - L'A' + L'a');
						else if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)
							c = static_cast<wchar_t>(c + 0x20);
					}

					return result;
				}

				bool IsSlash(wchar_t c)
				{
					return c == L'\\' || c == L'/';
				}

				// Turns `path` into a fully qualified, lower-case path.
				//
				// A relative path is resolved against the GAME directory, not
				// against the process's current directory: the two normally agree,
				// but a launcher is free to differ, and a wrong match here would
				// silently mean "never reload".
				bool MakeFullPath(const std::wstring& path, std::wstring& result)
				{
					std::wstring combined = path;

					const bool absolute =
						(path.size() >= 2 && path[1] == L':') ||
						(path.size() >= 1 && IsSlash(path[0]));

					if (!absolute)
						combined = GameDirectory + path;

					wchar_t buffer[1024] = { 0 };
					const DWORD length = ::GetFullPathNameW(
						combined.c_str(), _countof(buffer), buffer, nullptr);

					if (length == 0 || length >= _countof(buffer))
						return false;

					result = Lowercase(std::wstring(buffer, length));
					return true;
				}

				// True when `fullPath` sits inside `directory`.
				//
				// Both are lower case and fully qualified, and `directory` is
				// the game directory WITH its trailing separator - that
				// separator is what makes the prefix unambiguous, so nothing
				// more has to be checked here. (`C:\game2\x.ini` does not start
				// with `c:\game\`, so it is correctly rejected.)
				bool IsInsideDirectory(const std::wstring& fullPath,
					const std::wstring& directory)
				{
					if (fullPath.size() <= directory.size())
						return false;

					return fullPath.compare(0, directory.size(), directory) == 0;
				}

				bool HasAcceptedSuffix(const std::wstring& path)
				{
					if (path.size() < 4)
						return false;

					const std::wstring tail = Lowercase(path.substr(path.size() - 4));
					return tail == AcceptedSuffix;
				}
			}

			const char* TargetListFileName()
			{
				return ListFileName;
			}

			bool Load(const wchar_t* gameDirectory, const wchar_t* listFilePathOverride)
			{
				TargetCount = 0;
				EnabledFlag = false;
				GameDirectory.clear();

				if (!gameDirectory || !*gameDirectory)
				{
					LogWarn(L"IniHotReload: no game directory, feature off.");
					return false;
				}

				GameDirectory = gameDirectory;
				if (!GameDirectory.empty() && !IsSlash(GameDirectory.back()))
					GameDirectory.push_back(L'\\');

				const std::wstring configPath = listFilePathOverride
					? std::wstring(listFilePathOverride)
					: (GameDirectory + Widen(ListFileName));

				INIConfigReader reader;
				if (!reader.LoadFile(configPath.c_str()))
				{
					// Not an error: the feature is opt-in, and a user who never
					// heard of it must not see a warning on every start.
					LogDebug(L"IniHotReload: %ls not found, feature off.", configPath.c_str());
					return false;
				}

				if (!reader.HasSection(SectionName))
				{
					LogWarn(L"IniHotReload: %ls has no [%hs] section, feature off.",
						configPath.c_str(), SectionName);
					return false;
				}

				if (!reader.GetBool(SectionName, "Enabled", false))
				{
					LogDebug(L"IniHotReload: disabled by %ls (Enabled=no).", configPath.c_str());
					return false;
				}

				const std::string files = reader.GetString(SectionName, FilesKey, "");
				if (files.empty())
				{
					LogWarn(L"IniHotReload: Enabled=yes but %hs is empty, feature off.",
						FilesKey);
					return false;
				}

				// Which type kinds may be refed. Absent (or unrecognised) means the
				// verified default: buildings only.
				const std::string types = reader.GetString(SectionName, TypesKey, "");
				ParseTypeKinds(types);
				BuildTypeKindSummary();

				std::vector<std::string> items;
				SplitList(files, items);

				for (const std::string& item : items)
				{
					if (TargetCount >= MaxTargets)
					{
						LogWarn(L"IniHotReload: more than %u entries, the rest is ignored.",
							static_cast<unsigned>(MaxTargets));
						break;
					}

					const std::wstring path = Widen(item.c_str());

					if (!HasAcceptedSuffix(path))
					{
						LogWarn(L"IniHotReload: '%hs' is ignored - only %ls files are accepted.",
							item.c_str(), AcceptedSuffix);
						continue;
					}

					std::wstring fullPath;
					if (!MakeFullPath(path, fullPath))
					{
						LogWarn(L"IniHotReload: '%hs' is ignored - the path could not be resolved.",
							item.c_str());
						continue;
					}

					// The game directory itself always ends with a separator, so
					// its lower-case form is the prefix to test against.
					if (!IsInsideDirectory(fullPath, Lowercase(GameDirectory)))
					{
						LogWarn(L"IniHotReload: '%hs' is ignored - it is outside the game directory.",
							item.c_str());
						continue;
					}

					const DWORD attributes = ::GetFileAttributesW(fullPath.c_str());
					if (attributes == INVALID_FILE_ATTRIBUTES ||
						(attributes & FILE_ATTRIBUTE_DIRECTORY))
					{
						LogWarn(L"IniHotReload: '%hs' is ignored - no such file.", item.c_str());
						continue;
					}

					Target& target = TargetArray[TargetCount];
					::strncpy_s(target.Name, item.c_str(), _TRUNCATE);
					target.FullPath = fullPath;
					++TargetCount;

					LogDebug(L"IniHotReload: watching '%hs' (%ls).",
						item.c_str(), fullPath.c_str());
				}

				if (TargetCount == 0)
				{
					LogWarn(L"IniHotReload: no usable entry, feature off.");
					return false;
				}

				EnabledFlag = true;
				LogInfo(L"IniHotReload: %u file(s) watched: %hs",
					static_cast<unsigned>(TargetCount), files.c_str());
				LogInfo(L"IniHotReload: type kind(s) refed: %hs",
					TypeKindSummaryText);

				return true;
			}

			bool ShouldReloadType(unsigned abstractType)
			{
				if (abstractType >= TypeKindCount)
					return false;

				return TypeKindEnabled[abstractType];
			}

			const char* TypeKindSummary()
			{
				return TypeKindSummaryText;
			}

			size_t Count()
			{
				return TargetCount;
			}

			const Target& Get(size_t index)
			{
				static const Target empty = {};
				return index < TargetCount ? TargetArray[index] : empty;
			}

			bool Matches(const std::wstring& eventPath)
			{
				const std::wstring lowered = Lowercase(eventPath);

				for (size_t i = 0; i < TargetCount; ++i)
				{
					if (TargetArray[i].FullPath == lowered)
						return true;
				}

				return false;
			}

			bool IsEnabled()
			{
				return EnabledFlag;
			}
		}
	}
}
