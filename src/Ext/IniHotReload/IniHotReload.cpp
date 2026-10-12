#include <Ext/IniHotReload/IniHotReload.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Ext/IniHotReload/IniReloadTarget.h>
#include <Ext/IniHotReload/IniSnapshot.h>
#include <Ext/IniHotReload/IniWatchThread.h>

#include <AbstractTypeClass.h>
#include <CCINIClass.h>
#include <HouseClass.h>
#include <ScenarioClass.h>

#include <cstdio>
#include <string>
#include <vector>

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			namespace
			{
				// How long a watched file must be left alone before its content is
				// considered final. Editors write in bursts (truncate, write,
				// flush), and a save that lands in the middle of one would hand
				// the engine half a file.
				const double QuietSeconds = 1.5;

				// Minimum spacing between two reloads. A reload walks the type
				// list, so a burst of saves must not turn into a burst of walks.
				const double MinimumReloadInterval = 1.0;

				// Reading the INI files is what the reload costs; the rest is a
				// walk over the type array. Measured, the read dominates (see
				// docs\IniHotReload.md), so it is the part worth avoiding - hence
				// the section-diff based refeed below.
				const size_t MaxSectionsToDiff = 4000;

				// HOW MANY TYPES ARE REFED
				//
				// The list lives in karst.hotreload.ini (IniReloadTarget::Load
				// parses it) because the safe set has to be established by
				// measurement: each type's LoadFromINI runs engine and extension
				// code that was only ever written to run during load. The default
				// is the kind that has been verified in a live game (buildings),
				// and TerrainType is refused outright - the reference
				// implementation measured a crash there.
				bool WantsRefeed(AbstractType type)
				{
					// Declared in IniReloadTarget.h, defined in
					// IniReloadTarget.cpp; both live in this namespace.
					return ShouldReloadType(static_cast<unsigned>(type));
				}

				// The type name, for logging. AbstractClass::GetClassName reads a
				// static engine table, so it is safe for any value and never
				// touches the object.
				const char* TypeName(AbstractType type)
				{
					const char* name = AbstractClass::GetClassName(type);
					return name ? name : "?";
				}

				// The type's INI identifier (AbstractTypeClass::ID, +0x24), for
				// reporting which type a failure belonged to.
				void TypeId(AbstractTypeClass* pType, char* out, size_t outSize)
				{
					if (outSize == 0)
						return;

					if (MemorySafety::IsReadable(
							reinterpret_cast<const char*>(pType) + 0x24, 0x18))
					{
						_snprintf_s(out, outSize, _TRUNCATE, "%s", pType->ID);
					}
					else
					{
						_snprintf_s(out, outSize, _TRUNCATE, "<unreadable %p>", pType);
					}
				}

				bool SamePath(const char* left, const char* right)
				{
					if (!left || !right)
						return false;

					return ::_stricmp(left, right) == 0;
				}

				bool Initialized = false;
				bool Active = false;
				double LastReloadTime = 0.0;

				// The last content read from each file, used to work out which
				// sections an edit actually touched. Indexed like Target.
				IniSnapshot LastFileSnapshot[MaxTargets];
				IniSnapshot LastMapSnapshot;

				// Distribution of the engine's type list, logged once.
				const size_t TypeCount = 74;   // AbstractClass::GetClassName's table size
				unsigned TypeSeen[74] = { 0 };

				// KARST ADDITION: how many types of each kind were actually
				// refed this time, as opposed to how many exist (TypeSeen).
				//
				// This is the evidence a staged whitelist rollout needs. The
				// summary line only gives a total, and a total that includes
				// buildings cannot tell you whether units were among them.
				unsigned TypeRefed[74] = { 0 };
				bool DistributionLogged = false;

				double NowSeconds()
				{
					return static_cast<double>(::GetTickCount64()) / 1000.0;
				}

				bool GetGameDirectory(std::wstring& result)
				{
					wchar_t path[MAX_PATH] = { 0 };

					const DWORD length = ::GetModuleFileNameW(
						nullptr, path, _countof(path));

					if (length == 0 || length >= _countof(path))
						return false;

					std::wstring full(path, length);
					const size_t slash = full.find_last_of(L"\\/");

					if (slash == std::wstring::npos)
						return false;

					result = full.substr(0, slash + 1);
					return true;
				}

				// Is the engine far enough along for a refeed? Reloading while the
				// engine is still reading its own rules would race it, so the
				// reload waits until a match is running: the global player object
				// exists and the scenario has a file name.
				bool IsEngineReadyForReload()
				{
					HouseClass* player =
						MemorySafety::SafeReadReference(&HouseClass::Player);

					if (!player)
						return false;

					ScenarioClass* scenario = ScenarioClass::Global();
					if (!MemorySafety::IsReadable(scenario, sizeof(ScenarioClass*)))
						return false;

					return scenario->FileName[0] != '\0';
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
					::MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], needed);
					return result;
				}

				// The whole watched file as text, for the section comparison.
				//
				// Read from disk rather than out of the engine's INI object: the
				// comparison needs the file's own content, and a text read is a
				// few milliseconds for a 750 KB file. Returns false when the file
				// is absent or empty.
				bool ReadFileText(const char* name, std::string& out)
				{
					out.clear();

					const std::wstring wide = Widen(name);
					if (wide.empty())
						return false;

					HANDLE file = ::CreateFileW(wide.c_str(), GENERIC_READ,
						FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL, nullptr);

					if (file == INVALID_HANDLE_VALUE)
						return false;

					char buffer[64 * 1024];
					DWORD read = 0;
					bool ok = true;

					for (;;)
					{
						if (!::ReadFile(file, buffer, sizeof(buffer), &read, nullptr))
						{
							ok = false;
							break;
						}

						if (read == 0)
							break;

						out.append(buffer, read);
					}

					::CloseHandle(file);
					return ok && !out.empty();
				}

				// The map path as a full, lower-case path, for matching against the
				// configured list.
				std::wstring ResolveMapPath(const char* fileName)
				{
					wchar_t gameDirectory[MAX_PATH] = { 0 };
					::GetModuleFileNameW(nullptr, gameDirectory, MAX_PATH);

					std::wstring base(gameDirectory);
					const size_t slash = base.find_last_of(L"\\/");
					base = (slash == std::wstring::npos) ? std::wstring()
														 : base.substr(0, slash + 1);

					std::wstring combined = base + Widen(fileName);

					wchar_t resolved[1024] = { 0 };
					const DWORD length = ::GetFullPathNameW(
						combined.c_str(), _countof(resolved), resolved, nullptr);

					if (length == 0 || length >= _countof(resolved))
						return std::wstring();

					std::wstring result(resolved, length);
					for (wchar_t& c : result)
					{
						if (c >= L'A' && c <= L'Z')
							c = static_cast<wchar_t>(c - L'A' + L'a');
					}

					return result;
				}

				// --- engine calls, SEH-guarded ----------------------------------
				//
				// The engine calls below are wrapped in SEH: a type object or a
				// file object in an unexpected state faults instead of throwing,
				// and a fault must not take the whole game down. They live in
				// their own functions because MSVC refuses __try in a function
				// that also has to run destructors for local objects (C2712),
				// and keeping them free of C++ objects is what makes that work.

				CCINIClass* OpenTemporaryINI()
				{
					__try
					{
						return GameCreate<CCINIClass>();
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return nullptr;
					}
				}

				void CloseTemporaryINI(CCINIClass*& pINI)
				{
					if (!pINI)
						return;

					__try
					{
						CCINIClass::UnloadINIFile(pINI);
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						pINI = nullptr;
					}
				}

				// Reads every configured file into `pINI`, which is the INI object
				// the types will be refed from. Returns how many were read.
				//
				// Deliberately does NOT read the current map: the map is only read
				// when the map file itself is what changed (see OnFrame), because
				// a map's sections fed into this object would make every one of its
				// keys look "changed" against a baseline taken from the rules.
				unsigned ReadConfiguredFiles(CCINIClass* pINI)
				{
					unsigned loaded = 0;

					__try
					{
						// In the order the user listed them: reading an INI merges
						// into the object, so a later file overrides an earlier one.
						for (size_t i = 0; i < Count(); ++i)
						{
							CCFileClass* pFile = GameCreate<CCFileClass>(Get(i).Name);

							if (!pFile)
							{
								LogWarn(L"IniHotReload: '%hs' could not be opened.",
									Get(i).Name);
								continue;
							}

							if (pFile->Exists())
							{
								pINI->ReadCCFile(pFile);
								++loaded;
							}
							else
							{
								LogWarn(L"IniHotReload: '%hs' does not exist.",
									Get(i).Name);
							}

							GameDelete(pFile);
						}
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						LogError(L"IniHotReload: reading the INI files faulted; "
							L"the reload was abandoned.");
						return 0;
					}

					return loaded;
				}

				unsigned ReadMapFile(CCINIClass* pINI, const char* fileName)
				{
					unsigned loaded = 0;

					__try
					{
						CCFileClass* pMap = GameCreate<CCFileClass>(fileName);

						if (pMap)
						{
							if (pMap->Exists())
							{
								pINI->ReadCCFile(pMap);
								++loaded;
							}

							GameDelete(pMap);
						}
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						LogError(L"IniHotReload: reading the map INI faulted.");
						return 0;
					}

					return loaded;
				}

				void AppendFailedName(char* failedNames, size_t failedNamesSize,
					AbstractTypeClass* pType, unsigned failed)
				{
					if (!failedNames || failedNamesSize == 0 || failed > 8)
						return;

					char id[64] = { 0 };
					TypeId(pType, id, sizeof(id));

					if (failedNames[0] != '\0')
						::strncat_s(failedNames, failedNamesSize, ", ", _TRUNCATE);

					::strncat_s(failedNames, failedNamesSize, id, _TRUNCATE);
				}

				// Refeeds the type objects named by `sections`, or every type of a
				// whitelisted kind when `fullSweep` is set.
				//
				// The walk always visits the whole type array, because that is the
				// only way to reach the type objects - but LoadFromINI, the part
				// that costs time, runs only for the types that match.
				void RefeedTypes(CCINIClass* pINI, bool fullSweep,
					const std::vector<ChangedSection>& sections,
					unsigned& reloaded, unsigned& failed, unsigned& skipped,
					unsigned& total, unsigned* distribution,
					char* failedNames, size_t failedNamesSize)
				{
					reloaded = 0;
					failed = 0;
					skipped = 0;
					total = 0;

					for (size_t i = 0; i < 74; ++i)
						TypeRefed[i] = 0;

					if (failedNames && failedNamesSize > 0)
						failedNames[0] = '\0';

					// The type list is an engine global that only exists once the
					// engine has built its type objects. Reading a null one here
					// would fault before the per-type guard below could help.
					DynamicVectorClass<AbstractTypeClass*>* const typeArray =
						MemorySafety::SafeReadSlot<
							DynamicVectorClass<AbstractTypeClass*>*>(
								&AbstractTypeClass::Array);

					if (!typeArray)
					{
						LogWarn(L"IniHotReload: the engine's type list is not "
							L"available yet; the reload was skipped.");
						return;
					}

					for (AbstractTypeClass* pType : *typeArray)
					{
						++total;

						if (!MemorySafety::IsReadable(pType))
						{
							++failed;
							continue;
						}

						const AbstractType type = pType->WhatAmI();

						// Counted for every type, and only until the first
						// distribution report is written: the report describes the
						// engine's list, not the configured whitelist, so it stays
						// useful whatever the whitelist happens to be.
						const unsigned typeIndex = static_cast<unsigned>(type);
						if (distribution && !DistributionLogged &&
							typeIndex < TypeCount)
						{
							++distribution[typeIndex];
						}

						// Types outside the configured kinds are left alone.
						if (!WantsRefeed(type))
							continue;

						if (!fullSweep)
						{
							// Match by the type's own ID, which is the section name
							// the engine reads it from.
							const char* id = pType->get_ID();
							bool matched = false;

							for (const ChangedSection& section : sections)
							{
								if (::_stricmp(section.Name.c_str(), id) == 0)
								{
									matched = true;
									break;
								}
							}

							if (!matched)
							{
								++skipped;
								continue;
							}
						}

						// A type object's LoadFromINI is engine code that was
						// written to run during load. It is called here on the game
						// thread, at a frame boundary, with no engine call in
						// flight - but a fault from an unexpected object state is
						// still possible, and an access violation is not something a
						// C++ catch can see. SEH keeps one bad type from taking the
						// whole game down.
						bool ok = false;
						__try
						{
							ok = pType->LoadFromINI(pINI);
						}
						__except (EXCEPTION_EXECUTE_HANDLER)
						{
							ok = false;
						}

						if (ok)
						{
							++reloaded;

							if (typeIndex < 74)
								++TypeRefed[typeIndex];
						}
						else
						{
							// Named, not just counted: "4 failed" is not
							// actionable, but "GAPOWR, NAWALL, ..." is.
							// LoadFromINI answering false usually means "this type
							// has no section in the INI", which is normal for a
							// few placeholder types - but only names can tell.
							++failed;
							AppendFailedName(failedNames, failedNamesSize, pType, failed);
						}
					}
				}

				// Writes the type distribution to the log, once.
				void LogDistribution(const unsigned* distribution)
				{
					unsigned distinct = 0;

					for (size_t i = 0; i < TypeCount; ++i)
					{
						if (distribution[i] == 0)
							continue;

						++distinct;

						LogInfo(L"IniHotReload[types] %-22hs %u",
							TypeName(static_cast<AbstractType>(i)), distribution[i]);
					}

					LogInfo(L"IniHotReload[types] %u distinct type kind(s); widening "
						L"ShouldReloadType is a decision about which to refeed.",
						distinct);
				}

				// KARST ADDITION: which kinds were refed, per kind.
				//
				// Written after every reload that refed something, so an acceptance
				// run can confirm "UnitType really was among the kinds refed" rather
				// than inferring it from a total that also counts buildings.
				void LogRefeedByKind()
				{
					unsigned distinct = 0;

					for (size_t i = 0; i < 74; ++i)
					{
						if (TypeRefed[i] == 0)
							continue;

						++distinct;

						LogInfo(L"IniHotReload[refed] %-22hs %u",
							TypeName(static_cast<AbstractType>(i)), TypeRefed[i]);
					}

					LogInfo(L"IniHotReload[refed] %u kind(s) refed this time.",
						distinct);
				}

				// Works out which sections of which configured file changed since
				// the last reload, and moves the stored snapshots forward.
				//
				// Returns the total number of sections seen; `forceFullSweep` is
				// set when there is no usable baseline (the first reload ever),
				// where the only safe answer is "refeed everything".
				unsigned DiffAndAdvanceSnapshots(std::vector<ChangedSection>& sections,
					bool& forceFullSweep, std::string& summary)
				{
					unsigned totalSections = 0;
					forceFullSweep = false;

					for (size_t i = 0; i < Count(); ++i)
					{
						std::string text;
						IniSnapshot current;

						if (ReadFileText(Get(i).Name, text))
							BuildSnapshot(text, current);

						totalSections += static_cast<unsigned>(current.Sections.size());

						if (LastFileSnapshot[i].IsEmpty())
						{
							// No baseline for this file yet.
							if (!current.IsEmpty())
								forceFullSweep = true;
						}
						else
						{
							CollectChangedSections(LastFileSnapshot[i], current, sections);
						}

						LastFileSnapshot[i] = current;
					}

					// The map, when the map file is one of the watched files.
					ScenarioClass* scenario = ScenarioClass::Global();
					if (scenario && scenario->FileName[0] != '\0')
					{
						const std::wstring mapPath = ResolveMapPath(scenario->FileName);
						const bool watched = Matches(mapPath);

						if (watched)
						{
							std::string text;
							IniSnapshot current;

							if (ReadFileText(scenario->FileName, text))
								BuildSnapshot(text, current);

							totalSections += static_cast<unsigned>(
								current.Sections.size());

							if (LastMapSnapshot.IsEmpty())
							{
								if (!current.IsEmpty())
									forceFullSweep = true;
							}
							else
							{
								CollectChangedSections(
									LastMapSnapshot, current, sections);
							}

							LastMapSnapshot = current;
						}
					}

					// A short, human-readable account of the change set.
					summary.clear();
					char buffer[96] = { 0 };

					for (size_t i = 0; i < sections.size() && i < 12; ++i)
					{
						_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s%s(%s)",
							i > 0 ? ", " : "", sections[i].Name.c_str(),
							SectionChangeName(sections[i].Kind));
						summary += buffer;
					}

					if (sections.size() > 12)
						summary += ", ...";

					return totalSections;
				}
			}

			void Init()
			{
				if (Initialized)
					return;

				Initialized = true;

				std::wstring gameDirectory;
				if (!GetGameDirectory(gameDirectory))
				{
					LogWarn(L"IniHotReload: cannot determine the game directory, feature off.");
					return;
				}

				if (!Load(gameDirectory.c_str()))
					return;

				Active = StartWatchThread(gameDirectory.c_str());

				if (!Active)
					LogWarn(L"IniHotReload: the watcher could not be started, feature off.");
			}

			bool IsActive()
			{
				return Active;
			}

			void OnFrame()
			{
				if (!Active)
					return;

				// The whole per-frame cost while nothing is happening is this
				// one relaxed load.
				if (!HasPendingChanges())
					return;

				const double now = NowSeconds();

				if (now - LastReloadTime < MinimumReloadInterval)
					return;

				ChangedFile files[MaxTargets];
				const size_t changed = TakeQuietChangedFiles(
					files, MaxTargets, QuietSeconds);

				if (changed == 0)
					return;

				for (size_t i = 0; i < changed; ++i)
					LogInfo(L"IniHotReload: '%hs' changed, reloading.", files[i].Name);

				if (!IsEngineReadyForReload())
				{
					// No match in progress yet: leave the edits for a later frame
					// rather than refeeding the engine while it is still loading.
					LogDebug(L"IniHotReload: the engine is not ready yet, retrying.");
					return;
				}

				LastReloadTime = now;

				const double started = NowSeconds();

				// 1. Which sections changed? This reads the watched files as text.
				std::vector<ChangedSection> sections;
				std::string summary;
				bool forceFullSweep = false;

				const unsigned totalSections = DiffAndAdvanceSnapshots(
					sections, forceFullSweep, summary);

				const bool fullSweep = forceFullSweep ||
					sections.size() > MaxSectionsToDiff;

				if (sections.empty() && !fullSweep)
				{
					LogInfo(L"IniHotReload: %u section(s) seen, none changed - "
						L"nothing to refeed.", totalSections);
					return;
				}

				// The logger's wide overload needs wide text; the summary is built
				// from section names, which are ASCII.
				std::wstring summaryWide;
				summaryWide.reserve(summary.size());
				for (char c : summary)
				{
					summaryWide.push_back(
						static_cast<wchar_t>(static_cast<unsigned char>(c)));
				}

				LogInfo(L"IniHotReload: %u section(s) seen, %u changed%s%s%s",
					totalSections, static_cast<unsigned>(sections.size()),
					fullSweep ? L" - full sweep" : L"",
					summary.empty() ? L"" : L": ",
					summary.empty() ? L"" : summaryWide.c_str());

				// 2. Read the configured files into a temporary INI object and
				//    refeed the affected types from it.
				CCINIClass* pINI = OpenTemporaryINI();

				if (!pINI)
				{
					LogError(L"IniHotReload: could not create the temporary INI object.");
					return;
				}

				const unsigned loadedFiles = ReadConfiguredFiles(pINI);

				// The map is read too when the map itself is a watched file and it
				// changed (a map's sections override the global rules).
				ScenarioClass* scenario = ScenarioClass::Global();
				unsigned mapFiles = 0;

				if (loadedFiles > 0 && scenario && scenario->FileName[0] != '\0')
				{
					const std::wstring mapPath = ResolveMapPath(scenario->FileName);
					if (Matches(mapPath))
						mapFiles = ReadMapFile(pINI, scenario->FileName);
				}

				unsigned reloaded = 0;
				unsigned failed = 0;
				unsigned skipped = 0;
				unsigned total = 0;
				char failedNames[512] = { 0 };

				RefeedTypes(pINI, fullSweep, sections, reloaded, failed, skipped,
					total, TypeSeen, failedNames, sizeof(failedNames));

				CloseTemporaryINI(pINI);

				const double elapsedMs = (NowSeconds() - started) * 1000.0;

				LogInfo(L"IniHotReload: %u file(s) read, %u type(s) refed, "
					L"%u failed, %u skipped out of %u in %.1f ms.%s",
					loadedFiles + mapFiles, reloaded, failed, skipped, total,
					elapsedMs,
					elapsedMs > 100.0
						? L" That was slow - consider shortening the list." : L"");

				if (failed > 0 && failedNames[0] != '\0')
					LogInfo(L"IniHotReload: failed type(s): %hs", failedNames);

				if (reloaded > 0)
					LogRefeedByKind();

				if (loadedFiles == 0)
					LogWarn(L"IniHotReload: no file could be read; nothing was applied.");

				// Once per process: what the engine's type list actually holds.
				if (!DistributionLogged && fullSweep && total > 0)
				{
					DistributionLogged = true;
					LogDistribution(TypeSeen);
				}
			}

			void Shutdown()
			{
				if (!Initialized)
					return;

				StopWatchThread();

				Active = false;
				Initialized = false;
				LastReloadTime = 0.0;

				for (size_t i = 0; i < MaxTargets; ++i)
					LastFileSnapshot[i].Clear();

				LastMapSnapshot.Clear();
			}
		}
	}
}
