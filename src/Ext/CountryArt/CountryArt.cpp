#include <Ext/CountryArt/CountryArt.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Karst.h>

// GameClasses.h pulls the engine classes in dependency order; including
// BuildingTypeClass.h on its own leaves base classes incomplete.
#include <GameClasses.h>
#include <BuildingTypeClass.h>
#include <HouseClass.h>
#include <HouseTypeClass.h>
#include <CCINIClass.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ShowByCountries / ShowBySHP - per-owning-country building appearance.
//
// Self-contained on purpose: it does not include, call or change anything in
// Ext\ShowByCountry (ShowByArt), and that module does not know it exists. See
// the header for why the feature needs its own mechanism and why the copies are
// rebuilt for every match.
//
// The engine offsets below were each read out of disassembled gamemd.exe 1.001;
// the comment on every one names the instruction it came from, because a wrong
// offset here writes into an unrelated part of a type object.
// ---------------------------------------------------------------------------

namespace Karst
{
	namespace Ext
	{
		namespace CountryArt
		{
			namespace
			{
				// ===========================================================
				// Engine layout
				// ===========================================================

				// AbstractTypeClass::ID (char[0x18]) - also the building's own
				// INI section name.
				constexpr unsigned Type_ID = 0x24;

				// ObjectTypeClass::ImageFile (char[0x19]). The ART parser takes
				// every art key from the section this names:
				//   0x46181C  lea eax,[ebp+0x1f8]     ; ... then "ActiveAnim" etc.
				constexpr unsigned Type_ImageFile = 0x1F8;
				constexpr unsigned Type_ImageFileCapacity = 0x19;

				// The "already loaded" flags the engine clears before re-running a
				// parse: 0x45F23E tests the first (partial loader), 0x45EB2C clears
				// the second on the full-parse path.
				constexpr unsigned Type_ArtLoadedFlag = 0x1761;
				constexpr unsigned Type_ArtReloadFlag = 0xE04;

				// BuildingTypeClass::BuildingAnim[] - base 0xF4C, stride 0x44; slot 3
				// is ActiveAnim, whose name the parser stores at 0x1018 (0x4617F1).
				// Used by the diagnostics that show a copy took the target section's
				// animation vocabulary, not by the redirection itself.
				constexpr unsigned Type_ActiveAnimName = 0xF4C + 3 * 0x44;  // 0x1018

				// The engine allocates a BuildingTypeClass with `push 0x1798`
				// (0x465416, 0x466053, 0x6BBAC7, 0x6C29BC). YRpp's sizeof() is larger,
				// so it must not be used to size (or copy) this object.
				constexpr unsigned TypeObjectSize = 0x1798;
				constexpr unsigned EngineAllocate = 0x7C8E17;

				// The engine's complete ART parser. Its entry (0x45FE50) is already
				// hooked by ShowByArt, so the BODY is called here - 0x45FE56, past the
				// replaced `sub esp,0x358`. Re-entering the hooked entry would run
				// that trampoline a second time for no reason.
				constexpr unsigned FullArtParseBody = 0x45FE56;

				// HouseClass::Type - the HouseTypeClass* (the country):
				//   0x4F7186  mov [esi+0x34],ebx      ; its constructor
				constexpr unsigned House_Type = 0x34;

				// The footprint is an index PLUS two precomputed pointers. Changing
				// the index alone has no effect, so all five travel together.
				constexpr unsigned FootprintFields[] = {
					0xEE8,  // layout index
					0xEF0,  // Foundation index
					0xEF4,  // companion field
					0xDFC,  // precomputed grid pointer (table 0x89C900)
					0xED4,  // precomputed grid pointer (table 0x89D368)
				};

				// TechnoTypeClass's sidebar icon: the file name, the loaded SHP and the
				// "we allocated it" flag, plus the Alt versions. They are contiguous and
				// end exactly where PowersUpBuilding begins, so the whole block travels
				// as one:
				//
				//   0x6D6  CameoFile[0x19]     0x6F0  Cameo (SHPStruct*)  0x6F4  CameoAllocated
				//   0x6F5  AltCameoFile[0x19]  0x710  AltCameo            0x714  AltCameoAllocated
				//   0x715  PowersUpBuilding[0x18]
				constexpr unsigned Cameo_First = 0x6D6;
				constexpr unsigned Cameo_SHP = 0x6F0;
				constexpr unsigned Cameo_Last = 0x715;

				// Inline extension slots on the type object. A memcpy'd copy starts
				// out holding the source's values; the engine's parse of the copy can
				// overwrite them, so they are re-aligned to the CURRENT match's source
				// type right before the copy is handed to a building.
				//
				//   0x18   Phobos BuildingTypeExt  (Container.h: ExtPointerOffset)
				//   0xDF4  Phobos/Kratos TechnoTypeExt
				//   0xE24  Ares
				//   0xEA0  engine FreeUnit type (written by the ART parser, 0x46056D)
				struct ExtensionSlot
				{
					const char* owner;
					unsigned offset;
				};

				const ExtensionSlot ExtensionSlots[] = {
					{ "Phobos BuildingTypeExt", 0x18  },
					{ "TechnoTypeExt",          0xDF4 },
					{ "Ares",                   0xE24 },
					{ "FreeUnit",               0xEA0 },
				};

				constexpr size_t VALUE_BUFFER_SIZE = 2048;

				#define COUNTRYART_COUNTOF(array) (sizeof(array) / sizeof((array)[0]))

				// ===========================================================
				// Small helpers
				// ===========================================================

				bool IsSpaceChar(char c)
				{
					return c == ' ' || c == '\t' || c == '\r' || c == '\n';
				}

				std::string Trim(const std::string& value)
				{
					size_t begin = 0;
					size_t end = value.size();

					while (begin < end && IsSpaceChar(value[begin]))
						++begin;
					while (end > begin && IsSpaceChar(value[end - 1]))
						--end;

					return value.substr(begin, end - begin);
				}

				// Splits on commas, trims every entry and drops empty ones.
				// `hadEmpty` reports whether an empty entry was dropped (usually a
				// stray or trailing comma).
				std::vector<std::string> SplitList(const std::string& value, bool& hadEmpty)
				{
					std::vector<std::string> parts;
					hadEmpty = false;

					size_t start = 0;
					for (;;)
					{
						const size_t comma = value.find(',', start);
						const std::string raw = (comma == std::string::npos)
							? value.substr(start)
							: value.substr(start, comma - start);

						const std::string entry = Trim(raw);
						if (entry.empty())
							hadEmpty = true;
						else
							parts.push_back(entry);

						if (comma == std::string::npos)
							break;

						start = comma + 1;
					}

					return parts;
				}

				// Narrows to wide, for the logging helpers.
				std::wstring Widen(const char* text)
				{
					if (!text || !text[0])
						return std::wstring();

					const int length = ::MultiByteToWideChar(CP_ACP, 0, text, -1,
						nullptr, 0);
					if (length <= 1)
						return std::wstring();

					std::wstring wide(static_cast<size_t>(length - 1), L'\0');
					::MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], length);
					return wide;
				}

				// Copies a NUL-terminated string that lives at `offset` inside an
				// engine object. The bytes are proven readable first: these fields are
				// reached by offset, so a wrong one yields a wild pointer.
				bool ReadStringAt(const void* base, unsigned offset, char* out,
					size_t outCount)
				{
					if (!out || outCount == 0)
						return false;

					out[0] = '\0';

					if (!base)
						return false;

					const BYTE* text = static_cast<const BYTE*>(base) + offset;

					if (!Karst::MemorySafety::IsReadable(text, 1))
						return false;

					for (size_t i = 0; i + 1 < outCount; ++i)
					{
						if (!Karst::MemorySafety::IsReadable(text + i, 1))
							break;

						out[i] = static_cast<char>(text[i]);

						if (out[i] == '\0')
							return true;
					}

					out[outCount - 1] = '\0';
					return true;
				}

				// The two parallel lists, written in the building's own section.
				const char* const KEYS_COUNTRIES[] = {
					"ShowByCountries", "art.show_by_countries"
				};
				const char* const KEYS_SECTION[] = {
					"ShowBySHP", "art.show_by_shp"
				};

				// Reads a key, trying every accepted spelling of its name.
				bool ReadKey(CCINIClass* ini, const char* section,
					const char* const* keys, size_t keyCount, std::string& out)
				{
					char buffer[VALUE_BUFFER_SIZE];

					for (size_t i = 0; i < keyCount; ++i)
					{
						buffer[0] = '\0';
						const int length = ini->ReadString(section, keys[i], "",
							buffer, sizeof(buffer));

						if (length > 0 && buffer[0] != '\0')
						{
							out = Trim(buffer);
							return !out.empty();
						}
					}

					return false;
				}

				// Case-insensitive lookup in the engine's own country list, returning
				// the canonical spelling so the later comparison cannot depend on how
				// a mod author capitalised the name.
				const std::string* FindKnownCountry(
					const std::vector<std::string>& known, const std::string& name)
				{
					for (const std::string& candidate : known)
					{
						if (_stricmp(candidate.c_str(), name.c_str()) == 0)
							return &candidate;
					}

					return nullptr;
				}

				// True when `candidate` is one of the engine's own objects in that
				// array.
				//
				// Membership, not a pointer range: a live match keeps every
				// BuildingTypeClass / HouseClass / HouseTypeClass on the process heap,
				// far outside gamemd.exe's image, so "points into the executable" would
				// reject every real object.
				template <typename T>
				bool ArrayContains(DynamicVectorClass<T*>* const* arraySlot,
					const void* candidate)
				{
					if (!candidate)
						return false;

					auto* array = Karst::MemorySafety::SafeReadSlot<
						DynamicVectorClass<T*>*>(arraySlot);

					if (!array || array->Count <= 0)
						return false;

					for (int i = 0; i < array->Count; ++i)
					{
						if (static_cast<const void*>(array->Items[i]) == candidate)
							return true;
					}

					return false;
				}

				// The registered building type a section belongs to, if any. For
				// almost every building the section name IS the type ID.
				BuildingTypeClass* FindTypeBySection(const char* id)
				{
					if (!id || !id[0])
						return nullptr;

					auto* types = Karst::MemorySafety::SafeReadSlot<
						DynamicVectorClass<BuildingTypeClass*>*>(
							&BuildingTypeClass::Array);

					if (!types || types->Count <= 0)
						return nullptr;

					for (int i = 0; i < types->Count; ++i)
					{
						BuildingTypeClass* candidate = types->Items[i];

						if (candidate && Karst::MemorySafety::IsReadable(candidate)
							&& candidate->ID
							&& _stricmp(candidate->ID, id) == 0)
						{
							return candidate;
						}
					}

					return nullptr;
				}

				// Runs the engine's ART parser on `self` with `ini`, without going
				// through the hook on its entry point.
				//
				// WHAT THE STACK HAS TO LOOK LIKE
				//
				//   0x45FE50  sub  esp, 0x358           ; emulated here
				//   0x45FE56  push ebp                  ; <- entered here
				//   0x45FE57  push edi
				//   0x45FE58  mov  edi,[esp+0x364]      ; the ini argument
				//
				// At 0x45FE58 the frame is 8 bytes below the value 0x45FE56 is entered
				// with, so the argument belongs 0x364 - 0x358 - 8 = 4 bytes above that
				// ESP: pushing it immediately before the emulated `sub` does exactly
				// that. The epilogue (`add esp,0x358`, `ret 4`) unwinds both, so
				// `mov esp,ebp` below lands on the saved frame.
				bool __declspec(naked) CallParserBody(void* /*self*/, void* /*ini*/)
				{
					__asm
					{
						push ebp
						mov  ebp, esp
						mov  ecx, [ebp + 8]        // this = the type copy
						push dword ptr [ebp + 12]  // the CCINIClass* argument
						sub  esp, 0x358            // the instruction the hook replaced
						mov  eax, 0x45FE56         // == FullArtParseBody
						call eax
						movzx eax, al
						mov  esp, ebp
						pop  ebp
						ret
					}
				}

				// Fault isolation for the call above: the parser walks engine-owned
				// tables this module cannot verify, so a fault inside it must not take
				// the game down - a fault simply means the copy is not usable. The
				// wrapper is its own function because MSVC refuses __try in a function
				// that owns C++ objects (C2712).
				bool CallParserBodySafe(void* self, void* ini)
				{
					__try
					{
						return CallParserBody(self, ini);
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return false;
					}
				}

				// True when the engine handed us an INI object that is safe to parse
				// against. A low address is a temporary/stack object rather than a
				// real CCINIClass.
				bool UsableIni(const void* ini)
				{
					if (!ini)
						return false;

					const uintptr_t address = reinterpret_cast<uintptr_t>(ini);

					if (address < 0x00010000)
						return false;

					return Karst::MemorySafety::IsReadable(ini, 0x20);
				}

				// One (building, country) -> ART section rule.
				struct Copy
				{
					std::string countryId;   // canonical HouseTypeClass::ID
					std::string section;     // the ART section to use
					BuildingTypeClass* clone = nullptr;

					// Set once the footprint and the icon were taken from the target
					// section's own type, which can only happen after the engine has
					// parsed that type.
					bool takenFromTarget = false;
				};

				// One building type and everything declared for it.
				struct Group
				{
					std::string typeId;
					BuildingTypeClass* source = nullptr;  // the CURRENT match's object
					int generation = 0;
					std::vector<Copy> copies;
				};

				std::vector<Group> g_groups;
				bool g_rulesRead = false;
				int g_generations = 0;

				// The type the engine was given on the PREVIOUS call. The hook fires
				// at the START of a type's parse, so that previous type has finished by
				// now - which is the signal the footprint copy waits for. (A flag on
				// the type is not usable: +0x1761 belongs to the partial loader and the
				// full parser never sets it.)
				BuildingTypeClass* g_previousType = nullptr;

				int g_parseLogsLeft = 40;
				int g_useLogsLeft = 20;

				// Sum of every group's generation at the time of the last state
				// report; a change means the engine rebuilt its type list (a new
				// match), which is exactly when the report is worth printing again.
				int g_stateDumpStamp = 0;

				// ===========================================================
				// Reading the rules
				// ===========================================================

				int CountRules();

				// Reads ShowByCountries / ShowBySHP out of every building's own
				// section. A malformed entry is reported and DROPPED rather than left
				// half-applied.
				void ReadRules(CCINIClass* rulesINI, CCINIClass* artINI,
					DynamicVectorClass<BuildingTypeClass*>* types)
				{
					// The valid country names come from the engine itself, so they
					// always match the build in use - including countries a mod added.
					std::vector<std::string> knownCountries;

					auto* houseTypes = Karst::MemorySafety::SafeReadSlot<
						DynamicVectorClass<HouseTypeClass*>*>(&HouseTypeClass::Array);

					if (houseTypes && houseTypes->Count > 0)
					{
						for (int i = 0; i < houseTypes->Count; ++i)
						{
							HouseTypeClass* country = houseTypes->Items[i];

							if (country && Karst::MemorySafety::IsReadable(country)
								&& country->ID
								&& Karst::MemorySafety::IsReadable(country->ID, 1))
							{
								knownCountries.push_back(country->ID);
							}
						}
					}

					if (knownCountries.empty())
						return;   // retried on the next ART parse

					const CCINIClass* sources[2] = { rulesINI, artINI };

					// -----------------------------------------------------------
					// Scripts first.
					//
					// A script that gives a building a `byCountry` map is exactly
					// the per-owning-country form this module implements, so it
					// becomes the same Group/Copy structure. Scripts win over the
					// INI: a building a script mentions is not looked up in the INI
					// at all.
					//
					// A script that uses a single `look` is NOT handled here - that
					// is the global ShowByArt form, and one shared type cannot carry
					// a per-owner section name.
					// -----------------------------------------------------------
					const auto scriptHandles =
						[](const char* id) -> const Karst::Script::AppearanceRule*
						{
							if (!id)
								return nullptr;

							for (const Karst::Script::AppearanceRule& rule :
								KarstExtension::GetScriptAppearanceRules())
							{
								if (::_stricmp(rule.Building.c_str(), id) == 0)
									return &rule;
							}

							return nullptr;
						};

					int fromScripts = 0;

					for (const Karst::Script::AppearanceRule& rule :
						KarstExtension::GetScriptAppearanceRules())
					{
						if (rule.ByCountry.empty())
							continue;   // a single look belongs to ShowByArt

						Group group;
						group.typeId = rule.Building;

						for (const auto& entry : rule.ByCountry)
						{
							// The country has to be one the engine knows, exactly
							// as the INI form requires: a typo must be reported
							// rather than silently producing no art.
							const std::string* canonical = nullptr;

							for (const std::string& known : knownCountries)
							{
								if (::_stricmp(known.c_str(), entry.first.c_str()) == 0)
								{
									canonical = &known;
									break;
								}
							}

							if (!canonical)
							{
								Karst::LogInfo(L"CountryArt %hs: script says country "
									L"'%hs' but this build has no such country - the "
									L"entry is ignored.", rule.Building.c_str(),
									entry.first.c_str());
								continue;
							}

							Copy copy;
							copy.countryId = *canonical;
							copy.section = entry.second;
							group.copies.push_back(copy);
						}

						if (group.copies.empty())
						{
							Karst::LogInfo(L"CountryArt %hs: the script rule has no "
								L"usable country - it is ignored.",
								rule.Building.c_str());
							continue;
						}

						g_groups.push_back(group);
						++fromScripts;
					}

					if (fromScripts > 0)
					{
						Karst::LogInfo(L"CountryArt: %d script rule(s) applied ahead "
							L"of the INI.", fromScripts);
					}

					for (int i = 0; i < types->Count; ++i)
					{
						BuildingTypeClass* type = types->Items[i];

						if (!type || !Karst::MemorySafety::IsReadable(type)
							|| !type->ID)
						{
							continue;
						}

						// A script already decided this building; the INI does not
						// get a second say.
						if (scriptHandles(type->ID))
							continue;

						const char* section = type->ID;

						std::string countryList;
						std::string shpList;
						bool haveCountries = false;
						bool haveSection = false;

						for (const CCINIClass* source : sources)
						{
							if (!source)
								continue;

							if (!haveCountries
								&& ReadKey(const_cast<CCINIClass*>(source), section,
									KEYS_COUNTRIES, COUNTRYART_COUNTOF(KEYS_COUNTRIES),
									countryList))
							{
								haveCountries = true;
							}

							if (!haveSection
								&& ReadKey(const_cast<CCINIClass*>(source), section,
									KEYS_SECTION, COUNTRYART_COUNTOF(KEYS_SECTION),
									shpList))
							{
								haveSection = true;
							}
						}

						if (!haveCountries && !haveSection)
							continue;

						if (haveCountries != haveSection)
						{
							Karst::LogInfo(L"CountryArt %ls: ShowByCountries and "
								L"ShowBySHP must BOTH be present - the entry is ignored.",
								Widen(section).c_str());
							continue;
						}

						bool emptyCountries = false;
						bool emptySections = false;

						const std::vector<std::string> countries =
							SplitList(countryList, emptyCountries);
						const std::vector<std::string> sections =
							SplitList(shpList, emptySections);

						if (countries.empty() || sections.empty())
						{
							Karst::LogInfo(L"CountryArt %ls: an empty list - the entry "
								L"is ignored.", Widen(section).c_str());
							continue;
						}

						if (emptyCountries || emptySections)
						{
							Karst::LogInfo(L"CountryArt %ls: a list has an empty entry "
								L"(a stray comma?) - the entry is ignored.",
								Widen(section).c_str());
							continue;
						}

						if (countries.size() != sections.size())
						{
							Karst::LogInfo(L"CountryArt %ls: %d country name(s) but %d "
								L"SHP name(s) - the lists must line up - the entry is "
								L"ignored.", Widen(section).c_str(),
								static_cast<int>(countries.size()),
								static_cast<int>(sections.size()));
							continue;
						}

						Group group;
						group.typeId = section;

						bool problem = false;

						for (size_t k = 0; k < countries.size(); ++k)
						{
							const std::string* canonical =
								FindKnownCountry(knownCountries, countries[k]);

							if (!canonical)
							{
								Karst::LogInfo(L"CountryArt %ls: '%ls' is not a country "
									L"of this build - the entry is ignored.",
									Widen(section).c_str(),
									Widen(countries[k].c_str()).c_str());
								problem = true;
								break;
							}

							bool duplicate = false;

							for (const Copy& existing : group.copies)
							{
								if (_stricmp(existing.countryId.c_str(),
									canonical->c_str()) == 0)
								{
									duplicate = true;
									break;
								}
							}

							if (duplicate)
							{
								Karst::LogInfo(L"CountryArt %ls: '%ls' is listed twice - "
									L"the entry is ignored.", Widen(section).c_str(),
									Widen(countries[k].c_str()).c_str());
								problem = true;
								break;
							}

							Copy copy;
							copy.countryId = *canonical;
							copy.section = sections[k];
							group.copies.push_back(copy);
						}

						if (problem || group.copies.empty())
							continue;

						g_groups.push_back(group);
					}

					if (g_groups.empty())
					{
						g_rulesRead = true;
						Karst::LogDebug(L"CountryArt: no building declares "
							L"ShowByCountries - the feature stays out of the way.");
						return;
					}

					Karst::LogInfo(L"CountryArt: %d building(s) declare ShowByCountries, "
						L"%d rule(s) in total.", static_cast<int>(g_groups.size()),
						CountRules());

					for (const Group& group : g_groups)
					{
						for (const Copy& copy : group.copies)
						{
							Karst::LogDebug(L"CountryArt: %ls owned by %ls -> art "
								L"section '%ls'", Widen(group.typeId.c_str()).c_str(),
								Widen(copy.countryId.c_str()).c_str(),
								Widen(copy.section.c_str()).c_str());
						}
					}

					g_rulesRead = true;
				}

				// ===========================================================
				// The copies
				// ===========================================================

				// Allocates a copy of `source` whose ImageFile points at `section`.
				//
				// THE ID IS LEFT ALONE, and that matters: the engine reads `Image=`
				// and every rules key from the section named after the ID (+0x24) and
				// reads the ART keys from the section named after ImageFile (+0x1F8).
				// Keeping the source ID therefore keeps the source building's rules
				// (cost, health, prerequisites) and moves only the art.
				BuildingTypeClass* MakeCopy(BuildingTypeClass* source, const char* section)
				{
					if (!source || !section || !section[0])
						return nullptr;

					typedef void* (__cdecl * AllocateFunction)(size_t);

					AllocateFunction allocate = reinterpret_cast<AllocateFunction>(
						EngineAllocate);

					void* memory = allocate(TypeObjectSize);

					if (!memory)
					{
						Karst::LogInfo(L"CountryArt: the engine allocator returned "
							L"nothing for '%ls' - no copy was made.",
							Widen(section).c_str());
						return nullptr;
					}

					memcpy(memory, source, TypeObjectSize);

					BYTE* raw = static_cast<BYTE*>(memory);

					strncpy_s(reinterpret_cast<char*>(raw + Type_ImageFile),
						Type_ImageFileCapacity, section, _TRUNCATE);

					raw[Type_ArtLoadedFlag] = 0;
					raw[Type_ArtReloadFlag] = 0;

					return reinterpret_cast<BuildingTypeClass*>(memory);
				}

				// Runs ONE of the engine's parse passes on a copy, with the same INI
				// the engine is using on the source type at that moment.
				//
				// The parser's return value is NOT a verdict: the engine calls it with
				// INIs that do not contain the type at all, and it returns 0 for those
				// (the 0x410A86 bail-out). What matters is that the copy goes through
				// exactly the same reads as its source.
				void ParseCopy(BuildingTypeClass* clone, const char* section, void* ini)
				{
					if (!clone || !section || !section[0] || !ini)
						return;

					BYTE* raw = reinterpret_cast<BYTE*>(clone);

					// Re-assert the target section before each pass: the parser takes
					// the source's old ImageFile as the default for `Image=`.
					strncpy_s(reinterpret_cast<char*>(raw + Type_ImageFile),
						Type_ImageFileCapacity, section, _TRUNCATE);

					raw[Type_ArtLoadedFlag] = 0;
					raw[Type_ArtReloadFlag] = 0;

					const bool parsed = CallParserBodySafe(clone, ini);

					if (g_parseLogsLeft > 0)
					{
						--g_parseLogsLeft;

						char imageFile[64] = { 0 };
						char activeAnim[64] = { 0 };

						ReadStringAt(clone, Type_ImageFile, imageFile,
							sizeof(imageFile));
						ReadStringAt(clone, Type_ActiveAnimName, activeAnim,
							sizeof(activeAnim));

						Karst::LogDebug(L"CountryArt[parse] '%ls' on %p (ini=%p) -> "
							L"return=%d; ImageFile='%ls' ActiveAnim='%ls'",
							Widen(section).c_str(), static_cast<void*>(clone), ini,
							parsed ? 1 : 0, Widen(imageFile).c_str(),
							Widen(activeAnim).c_str());
					}
				}

				// Makes - or remakes - this building's copies from `source`.
				//
				// REBUILDING IS THE POINT: the engine gives a building type a new
				// object when a new match starts, so a copy made in the previous match
				// points at a match that no longer exists. Remaking happens here,
				// before the engine parses the source type, which is the only moment at
				// which a copy shares nothing loaded with its source.
				void MakeOrRemakeCopies(Group& group, BuildingTypeClass* source)
				{
					group.source = source;
					++group.generation;

					if (group.generation > 1)
					{
						++g_generations;
						Karst::LogDebug(L"CountryArt: the engine now gives %ls a new "
							L"type object (%p) - remaking %d copy(ies) for generation "
							L"%d. (A copy is never carried across matches.)",
							Widen(group.typeId.c_str()).c_str(),
							static_cast<void*>(source),
							static_cast<int>(group.copies.size()), group.generation);
					}
					else
					{
						Karst::LogDebug(L"CountryArt: %ls -> source type %p, making %d "
							L"copy(ies).", Widen(group.typeId.c_str()).c_str(),
							static_cast<void*>(source),
							static_cast<int>(group.copies.size()));
					}

					for (Copy& copy : group.copies)
					{
						// The old copy is left allocated rather than freed: a building
						// created in the previous match could still name it, and the
						// cost is one type object per rule per match.
						copy.clone = MakeCopy(source, copy.section.c_str());
						copy.takenFromTarget = false;

						if (!copy.clone)
							continue;

						Karst::LogDebug(L"CountryArt: %ls -> '%ls' copy %p (ID stays "
							L"'%ls', only ImageFile moves).",
							Widen(group.typeId.c_str()).c_str(),
							Widen(copy.section.c_str()).c_str(),
							static_cast<void*>(copy.clone),
							Widen(group.typeId.c_str()).c_str());
					}
				}

				// Mirrors one of the engine's parse calls onto every copy of `group`.
				void MirrorParse(Group& group, void* ini)
				{
					if (!UsableIni(ini))
						return;

					for (Copy& copy : group.copies)
					{
						if (copy.clone)
							ParseCopy(copy.clone, copy.section.c_str(), ini);
					}
				}

				// Gives a copy the sidebar icon of the type its section belongs to.
				//
				// The icon is a type-level pair - a file name plus the loaded SHP - that
				// the engine fills from the ART section's `Cameo=` key during its own
				// type load:
				//
				//   0x715997  push 0x19               ; CameoFile is char[0x19]
				//   0x71599F  push "Cameo"
				//   0x7159CF  lea ecx,[ebp+0x6d6]     ; ... the name is copied here
				//   0x715A22  mov eax,[ebp+0x6f0]     ; and the SHP is loaded only
				//   0x715A2D  jne ...                 ; while that pointer is STILL NULL
				//
				// A copy inherits its source's icon through memcpy, so that test always
				// fails and the engine never loads the target section's icon: the copy
				// keeps showing the SOURCE building's cameo (measured - an American
				// barracks drew YABRCK but kept GAPILE's side-bar icon). The block is
				// therefore taken from the target type, at the same moment as the
				// footprint.
				void CopyCameoFields(BuildingTypeClass* destination, const void* origin)
				{
					if (!destination || !origin)
						return;

					memcpy(reinterpret_cast<BYTE*>(destination) + Cameo_First,
						static_cast<const BYTE*>(origin) + Cameo_First,
						Cameo_Last - Cameo_First);
				}

				// Gives a copy the footprint AND the sidebar icon of the type its
				// section belongs to, once that type has actually been parsed.
				//
				// The copy is made while the engine is still walking its type list, so
				// the target type (NAHAND, NAREFN, ...) may not have been reached yet;
				// copying then would copy initial values (measured: a one-cell
				// footprint). `finished` is the type the previous hook call was given,
				// which is complete by now.
				void TakeTargetFields(BuildingTypeClass* finished)
				{
					if (!finished)
						return;

					for (Group& group : g_groups)
					{
						for (Copy& copy : group.copies)
						{
							if (!copy.clone || copy.takenFromTarget)
								continue;

							if (FindTypeBySection(copy.section.c_str()) != finished)
								continue;

							BYTE* target = reinterpret_cast<BYTE*>(copy.clone);
							const BYTE* origin = reinterpret_cast<const BYTE*>(finished);

							// Read BEFORE the copy, so the log shows the change rather
							// than two identical values.
							const DWORD cameoBefore = *reinterpret_cast<const DWORD*>(
								target + Cameo_SHP);

							for (unsigned offset : FootprintFields)
							{
								*reinterpret_cast<LONG*>(target + offset) =
									*reinterpret_cast<const LONG*>(origin + offset);
							}

							CopyCameoFields(copy.clone, finished);

							copy.takenFromTarget = true;

							Karst::LogDebug(L"CountryArt[target] %ls -> '%ls': layout "
								L"%d->%d, foundation %d->%d, cameo 0x%08X->0x%08X.",
								Widen(group.typeId.c_str()).c_str(),
								Widen(copy.section.c_str()).c_str(),
								*reinterpret_cast<const LONG*>(target + 0xEE8),
								*reinterpret_cast<const LONG*>(origin + 0xEE8),
								*reinterpret_cast<const LONG*>(target + 0xEF0),
								*reinterpret_cast<const LONG*>(origin + 0xEF0),
								cameoBefore,
								*reinterpret_cast<const DWORD*>(origin + Cameo_SHP));
						}
					}
				}

				// Keeps the inline extension slots of a copy in step with the source
				// type of the CURRENT match, right before the copy is used.
				//
				// A memcpy'd copy holds the source's pointers, which is what the
				// extensions expect (they read the slot, they do not look the copy up
				// in a table). The engine's own parse of the copy can overwrite the
				// slot in between, so it is re-checked here - and a slot that no longer
				// points at anything readable is restored even when the source's value
				// is null, because a wild slot is what a later crash is made of.
				void AlignExtensionSlots(Copy& copy, BuildingTypeClass* source)
				{
					if (!copy.clone || !source)
						return;

					for (const ExtensionSlot& slot : ExtensionSlots)
					{
						DWORD* target = reinterpret_cast<DWORD*>(
							reinterpret_cast<BYTE*>(copy.clone) + slot.offset);

						const DWORD desired = *reinterpret_cast<const DWORD*>(
							reinterpret_cast<const BYTE*>(source) + slot.offset);

						if (*target == desired)
							continue;

						const bool readable = *target
							&& Karst::MemorySafety::IsReadable(
								reinterpret_cast<const void*>(
									static_cast<uintptr_t>(*target)), 0x20);

						if (!readable)
						{
							const DWORD previous = *target;
							*target = desired;
							Karst::LogDebug(L"CountryArt[slots] %ls +0x%X: 0x%08X -> "
								L"0x%08X (source).", Widen(slot.owner).c_str(),
								slot.offset, previous, desired);
						}
					}
				}

				// The owning country's ID, read from the owning house.
				//
				// `owner` is the constructor's second stack argument, i.e. a raw
				// value: map scenery passes 0, and a wrong slot would be anything.
				// Both the house and the country must be members of the engine's own
				// arrays before the ID is read.
				bool OwnerCountryId(void* owner, char* out, size_t outCount)
				{
					if (!out || outCount == 0)
						return false;

					out[0] = '\0';

					if (!Karst::MemorySafety::IsReadable(owner, 0x40))
						return false;

					if (!ArrayContains<HouseClass>(&HouseClass::Array, owner))
						return false;

					const void* country = *reinterpret_cast<void* const*>(
						static_cast<const BYTE*>(owner) + House_Type);

					if (!Karst::MemorySafety::IsReadable(country, 0x40))
						return false;

					if (!ArrayContains<HouseTypeClass>(&HouseTypeClass::Array, country))
						return false;

					return ReadStringAt(country, Type_ID, out, outCount);
				}

				// The LOCAL player's country, cached because the cameo lookup runs on
				// every side-bar draw.
				//
				// HouseClass::Player (0xA83D4C) is what the engine's own cameo lookup
				// consults, so using the same pointer keeps the two in step. The cache
				// is keyed on the pointer: a new match brings a new player object.
				void* g_cachedPlayer = nullptr;
				std::string g_cachedPlayerCountry;

				const char* LocalPlayerCountry()
				{
					HouseClass* player = Karst::MemorySafety::SafeReadReference(
						&HouseClass::Player);

					if (!player)
					{
						g_cachedPlayer = nullptr;
						g_cachedPlayerCountry.clear();
						return nullptr;
					}

					if (player != g_cachedPlayer)
					{
						g_cachedPlayer = player;
						g_cachedPlayerCountry.clear();

						const void* country = *reinterpret_cast<void* const*>(
							reinterpret_cast<const BYTE*>(player) + House_Type);

						char id[64] = { 0 };

						if (Karst::MemorySafety::IsReadable(country, 0x40)
							&& ArrayContains<HouseTypeClass>(&HouseTypeClass::Array,
								country)
							&& ReadStringAt(country, Type_ID, id, sizeof(id)))
						{
							g_cachedPlayerCountry = id;

							Karst::LogDebug(L"CountryArt[cameo] local player %p is "
								L"country '%ls'.", static_cast<void*>(player),
								Widen(id).c_str());
						}
						else
						{
							Karst::LogDebug(L"CountryArt[cameo] local player %p has no "
								L"readable country - the side-bar icons stay as they "
								L"are.", static_cast<void*>(player));
						}
					}

					return g_cachedPlayerCountry.empty()
						? nullptr
						: g_cachedPlayerCountry.c_str();
				}

				int CountRules()
				{
					int total = 0;

					for (const Group& group : g_groups)
						total += static_cast<int>(group.copies.size());

					return total;
				}
			}

			// ===============================================================
			// Entry points
			// ===============================================================

			void OnArtParse(void* pType, void* ini)
			{
				if (!g_rulesRead)
				{
					auto* types = Karst::MemorySafety::SafeReadSlot<
						DynamicVectorClass<BuildingTypeClass*>*>(
							&BuildingTypeClass::Array);

					CCINIClass* rulesINI = Karst::MemorySafety::SafeReadSlot<
						CCINIClass*>(&CCINIClass::INI_Rules);
					CCINIClass* artINI = Karst::MemorySafety::SafeReadSlot<
						CCINIClass*>(&CCINIClass::INI_Art);

					if (types && types->Count > 0 && (rulesINI || artINI))
						ReadRules(rulesINI, artINI, types);
				}

				if (g_groups.empty())
					return;

				BuildingTypeClass* const type =
					reinterpret_cast<BuildingTypeClass*>(pType);

				if (!type)
					return;

				// The type the engine was given last time has finished its parse by
				// now, so its Foundation and its icon are final - that is the signal
				// the target-derived fields wait for.
				TakeTargetFields(g_previousType);
				g_previousType = type;

				Group* group = nullptr;

				for (Group& candidate : g_groups)
				{
					if (candidate.source == type)
					{
						group = &candidate;
						break;
					}
				}

				if (!group && type->ID)
				{
					for (Group& candidate : g_groups)
					{
						if (_stricmp(candidate.typeId.c_str(), type->ID) == 0)
						{
							group = &candidate;
							break;
						}
					}
				}

				if (!group)
					return;

				if (group->source != type)
					MakeOrRemakeCopies(*group, type);

				MirrorParse(*group, ini);
			}

			void* TypeForOwner(void* pType, void* pOwner)
			{
				if (g_groups.empty() || !pType || !pOwner)
					return pType;

				BuildingTypeClass* const type =
					reinterpret_cast<BuildingTypeClass*>(pType);

				Group* group = nullptr;

				for (Group& candidate : g_groups)
				{
					if (candidate.source == type)
					{
						group = &candidate;
						break;
					}
				}

				if (!group)
					return pType;

				char countryId[64] = { 0 };

				if (!OwnerCountryId(pOwner, countryId, sizeof(countryId)))
					return pType;

				for (Copy& copy : group->copies)
				{
					if (!copy.clone)
						continue;

					if (_stricmp(copy.countryId.c_str(), countryId) != 0)
						continue;

					// Last line of defence, immediately before the type is handed to
					// the building.
					AlignExtensionSlots(copy, group->source);

					if (g_useLogsLeft > 0)
					{
						--g_useLogsLeft;

						Karst::LogDebug(L"CountryArt[use] %ls owned by %ls -> copy %p "
							L"(section '%ls', generation %d).",
							Widen(group->typeId.c_str()).c_str(),
							Widen(countryId).c_str(),
							static_cast<void*>(copy.clone),
							Widen(copy.section.c_str()).c_str(), group->generation);
					}

					return copy.clone;
				}

				return pType;
			}

			void* TypeForCameo(void* pType)
			{
				if (g_groups.empty() || !pType)
					return nullptr;

				BuildingTypeClass* const type =
					reinterpret_cast<BuildingTypeClass*>(pType);

				Group* group = nullptr;

				for (Group& candidate : g_groups)
				{
					if (candidate.source == type)
					{
						group = &candidate;
						break;
					}
				}

				// Not a type this module configured: the caller keeps the type it
				// was asked about, so nothing else in the game is affected.
				if (!group)
					return nullptr;

				const char* const country = LocalPlayerCountry();

				if (!country || !country[0])
					return nullptr;

				for (Copy& copy : group->copies)
				{
					if (!copy.clone
						|| _stricmp(copy.countryId.c_str(), country) != 0)
					{
						continue;
					}

					// A copy whose icon is missing must not replace a working one:
					// the player would lose the entry instead of seeing it change.
					const DWORD cameo = *reinterpret_cast<const DWORD*>(
						reinterpret_cast<const BYTE*>(copy.clone) + Cameo_SHP);

					if (cameo)
						return copy.clone;
				}

				return nullptr;
			}

			void OnFrame()
			{
				if (g_groups.empty())
					return;

				// One report per generation. A new generation means the engine
				// rebuilt its type list, i.e. a new match started, which is exactly
				// when this report is worth having again.
				int stamp = 0;

				for (const Group& group : g_groups)
					stamp += group.generation;

				if (stamp == g_stateDumpStamp)
					return;

				g_stateDumpStamp = stamp;

				// One line per generation, and the only thing this module says during
				// normal play: it confirms the copies were built for THIS match.
				Karst::LogInfo(L"CountryArt: generation %d - %d building(s), %d "
					L"rule(s) active, %d rebuild(s) so far.", stamp,
					static_cast<int>(g_groups.size()), CountRules(), g_generations);

				for (const Group& group : g_groups)
				{
					Karst::LogDebug(L"CountryArt[state] %ls source=%p (generation %d)",
						Widen(group.typeId.c_str()).c_str(),
						static_cast<void*>(group.source), group.generation);

					for (const Copy& copy : group.copies)
					{
						if (!copy.clone)
						{
							Karst::LogDebug(L"CountryArt[state]   %ls: NO copy",
								Widen(copy.countryId.c_str()).c_str());
							continue;
						}

						char imageFile[64] = { 0 };
						char activeAnim[64] = { 0 };

						ReadStringAt(copy.clone, Type_ImageFile, imageFile,
							sizeof(imageFile));
						ReadStringAt(copy.clone, Type_ActiveAnimName, activeAnim,
							sizeof(activeAnim));

						Karst::LogDebug(L"CountryArt[state]   %ls -> %p section='%ls' "
							L"ImageFile='%ls' ActiveAnim='%ls'",
							Widen(copy.countryId.c_str()).c_str(),
							static_cast<void*>(copy.clone),
							Widen(copy.section.c_str()).c_str(),
							Widen(imageFile).c_str(), Widen(activeAnim).c_str());
					}
				}
			}
		}
	}
}
