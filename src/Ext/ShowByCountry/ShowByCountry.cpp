#include <Ext/ShowByCountry/ShowByCountry.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Ext/EngineLayout.h>
#include <Karst.h>

// GameClasses.h pulls in the engine classes in dependency order; including
// BuildingClass.h on its own leaves FootClass incomplete and the cast helpers do
// not compile.
#include <GameClasses.h>
#include <ObjectClass.h>
#include <TechnoClass.h>
#include <BuildingTypeClass.h>
#include <HouseTypeClass.h>
#include <CCINIClass.h>
#include <FileSystem.h>
#include <FileFormats/SHP.h>
#include <Memory.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace Karst
{
	namespace Ext
	{
		namespace ShowByCountry
		{
			namespace
			{
				// BuildingTypeClass::Array is `static DynamicVectorClass<...>* const`.
				// Naming the concrete type once keeps every access below readable and
				// avoids the template-deduction trouble that `&Array` (type `T* const*`)
				// causes for the generic slot reader.
				typedef DynamicVectorClass<BuildingTypeClass*> TypeList;
				typedef TypeList* TypeListPtr;

				// Set once Init() has found the engine's building type list, so the
				// main loop stops retrying.
				bool g_initialized = false;

				// The ShowByArt redirect list is read once per match, the first time
				// the engine parses a building's ART section (see
				// RedirectSectionBeforeFullParse - the only entry point that runs
				// early enough for the section name to still matter).
				bool g_redirectsParsed = false;

				// Bounded diagnostics for the redirect gate, so a mis-ordered latch is
				// visible in the log instead of silently disabling ShowByArt.
				int g_redirectGateReports = 0;

				// Keys that redirect a building to another building's art - and,
				// because a footprint is a rules matter, to that building's
				// foundation as well. Written in the building's own section, in
				// rulesmd.ini (or artmd.ini, which is checked second):
				//
				//     [NAPOWR]
				//     ShowByArt=GAPOWR          ; the imitated building
				//
				// The value is normally the imitated building's ART SECTION, which
				// is also its type ID for almost every building. When the two differ
				// the optional second key names the type:
				//
				//     [NAMISL]
				//     ShowByArt=GAWETH          ; art section (artmd.ini label)
				//     ShowByArtType=GAWEAT      ; the registered building type
				//
				// GAWETH/GAWEAT is a real case: [GAWEAT] carries Image=GAWETH, and
				// the Foundation lives in the [GAWETH] art section. Redirecting the
				// art needs GAWETH; copying the footprint needs a type that exists in
				// BuildingTypeClass::Array, which is GAWEAT.
				const char* const KEYS_ART[] = {
					"ShowByArt", "art.show_by_art", "ShowBySHPName", "ShowByBuilding"
				};
				const char* const KEYS_ART_TYPE[] = {
					"ShowByArtType", "art.show_by_art_type"
				};

				constexpr size_t VALUE_BUFFER_SIZE = 2048;

				#define KARST_COUNTOF(array) (sizeof(array) / sizeof((array)[0]))

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

				// Splits on commas, trims each entry, and drops empty entries.
				// `hadEmpty` reports whether an empty entry was dropped, which is
				// almost always a typo such as a trailing comma.
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

						std::string entry = Trim(raw);
						if (entry.empty())
						{
							// Ignore the trailing empty piece after the final comma
							// only when the whole remainder is empty.
							if (!(comma == std::string::npos && raw.empty() && !parts.empty()))
								hadEmpty = true;
						}
						else
						{
							parts.push_back(entry);
						}

						if (comma == std::string::npos)
							break;

						start = comma + 1;
					}

					return parts;
				}

				// Reads a key, trying every accepted spelling. Returns true when a
				// non-empty value was found.
				bool ReadKey(CCINIClass* ini, const char* section,
					const char* const* keys, size_t keyCount, std::string& out)
				{
					char buffer[VALUE_BUFFER_SIZE];

					for (size_t i = 0; i < keyCount; ++i)
					{
						buffer[0] = '\0';
						const int length = ini->ReadString(section, keys[i], "", buffer, sizeof(buffer));

						if (length > 0 && buffer[0] != '\0')
						{
							out = Trim(buffer);
							return !out.empty();
						}
					}

					return false;
				}

				// Is either spelling of the key present, even if its value is blank?
				bool KeyPresent(CCINIClass* ini, const char* section,
					const char* const* keys, size_t keyCount)
				{
					char buffer[VALUE_BUFFER_SIZE];

					for (size_t i = 0; i < keyCount; ++i)
					{
						buffer[0] = '\0';
						if (ini->ReadString(section, keys[i], "", buffer, sizeof(buffer)) > 0)
							return true;
					}

					return false;
				}


				std::string JoinNames(const std::vector<std::string>& names)
				{
					std::string joined;
					for (size_t i = 0; i < names.size(); ++i)
					{
						if (i)
							joined += ", ";
						joined += names[i];
					}
					return joined;
				}
			}





			namespace
			{

				std::wstring ToWide(const std::string& text)
				{
					if (text.empty())
						return std::wstring();

					const int length = ::MultiByteToWideChar(CP_UTF8, 0,
						text.c_str(), static_cast<int>(text.size()), nullptr, 0);
					if (length <= 0)
						return std::wstring();

					std::wstring wide(static_cast<size_t>(length), L'\0');
					::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
						static_cast<int>(text.size()), &wide[0], length);
					return wide;
				}

				std::wstring JoinProblems(const std::vector<std::string>& problems)
				{
					std::wstring joined;
					for (size_t i = 0; i < problems.size(); ++i)
					{
						joined += L"- ";
						joined += ToWide(problems[i]);
						joined += L"\n";
					}
					return joined;
				}
			}


			bool IsInitialized()
			{
				return g_initialized;
			}

			// The engine must have finished loading its type list before this module
			// can do anything useful. KarstExtension::OnGameStart runs at the
			// executable entry point (0x7CD810), far too early - the type list does
			// not exist yet - so returning false tells the caller to retry.
			//
			// This used to parse the ShowByCountries / ShowBySHP rules and build a
			// RuleSet. All of that is removed with the per-country feature. The
			// feature now reads ShowByArt, and it does so in
			// RedirectSectionBeforeFullParse() - the one entry point that actually
			// runs during map load and therefore the only place where the section
			// name can still influence what the engine parses.
			bool Init()
			{
				if (g_initialized)
					return true;

				TypeListPtr buildingTypes = BuildingTypeClass::Array;

				if (!buildingTypes || buildingTypes->Count <= 0)
					return false;

				g_initialized = true;

				Karst::LogInfo(L"ShowByCountry: ready - %d building type(s) available.",
					buildingTypes->Count);

				return true;
			}


			// -----------------------------------------------------------------
			// Runtime
			// -----------------------------------------------------------------
			//
			// The image is applied by walking BuildingClass::Array from the main
			// loop and pointing each building type at the SHP its owner's country
			// should use. No hook inside the draw path is involved, which matters:
			// two candidate draw-path hook addresses were tried in a live game and
			// neither routine ran for a deployed building. The main loop hook is the
			// one that is known to work.
			//
			// Consequence to be aware of: ImageFile/Image live on BuildingTypeClass,
			// which is shared by every instance of that type. If two countries in
			// one match own the same building type, the last one processed wins for
			// both. In the common case - each country builds its own structures -
			// that is invisible.

			namespace
			{
				// Original image name per building type, so the value can be restored
				// if the building's country changes or the rule stops applying.
				struct TypeImageState
				{
					int TypeIndex = -1;
					char OriginalImageFile[0x19] = { 0 };
					const char* AppliedShp = nullptr;

					// The Image pointer this module assigned, and how many times the
					// engine has been seen replacing it. Measuring this distinguishes
					// "the engine ignores Image" from "the engine overwrites it" - the
					// two possible reasons the picture never changes on screen.
					void* AppliedImage = nullptr;
					int EngineOverwrites = 0;
				};

				std::vector<TypeImageState> g_typeStates;


				// Building type indices already reported, one log line per type.
				std::vector<int> g_loggedTypes;

				// How many "which SHP does the engine actually use" dumps have
				// been written. The dump is the whole point of this revision: it
				// shows every image-bearing field of the type and what the engine's
				// own GetImage() returns, so "the picture did not change" can be
				// attributed to a named field instead of guessed at.
				int g_imageDumpsLeft = 6;

				// Reads a SHP struct's header and filename safely.
				//
				// Every field of ObjectTypeClass is reachable only by offset, and
				// a wrong offset yields a wild pointer. The header is therefore
				// read one member at a time through the readable-range check
				// instead of by dereferencing a struct whose layout is only
				// assumed. `filename` is optional: a SHPFile has no name, only a
				// SHPReference does.
				// Narrows->wide for diagnostics.
				//
				// Explicitly converting beats `%hs` here: every message is formatted
				// with the wide `_vsnwprintf_s`, and a narrow string handed to it that
				// way was observed to come out as mojibake - "ImageFile='NACNST'"
				// printed as CJK characters. Formatting the narrow text first and
				// passing the result with `%ls` is unambiguous.
				std::wstring Widen(const char* text)
				{
					if (!text || !text[0])
						return std::wstring();

					const int length = ::MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
					if (length <= 1)
						return std::wstring();

					std::wstring wide(static_cast<size_t>(length - 1), L'\0');
					::MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], length);
					return wide;
				}

				void DescribeShp(const void* shp, wchar_t* out, size_t outCount)
				{
					if (outCount == 0)
						return;

					out[0] = L'\0';

					if (!shp || !Karst::MemorySafety::IsReadable(shp))
					{
						::swprintf_s(out, outCount, L"(none)");
						return;
					}

					// SHPStruct: WORD Type; short Width; short Height; short Frames;
					const WORD* header = static_cast<const WORD*>(shp);
					if (!Karst::MemorySafety::IsReadable(header, sizeof(WORD) * 4))
					{
						::swprintf_s(out, outCount, L"%p (header unreadable)", shp);
						return;
					}

					const WORD type = header[0];
					const short width = *reinterpret_cast<const short*>(header + 1);
					const short height = *reinterpret_cast<const short*>(header + 2);
					const short frames = *reinterpret_cast<const short*>(header + 3);

					// A SHPReference (Type == 0xFFFF) keeps its filename at +0x08.
					if (type == 0xFFFF)
					{
						const DWORD* nameSlot = reinterpret_cast<const DWORD*>(
							static_cast<const BYTE*>(shp) + 8);

						// Copy the name with a hard bound instead of printing the
						// engine's pointer directly: the only thing proven about it is
						// that its first byte is readable, and `%hs` would happily run
						// off the end of a string that is not terminated. A diagnostic
						// must not be able to crash the match it is measuring.
						char name[128] = { 0 };
						bool haveName = false;

						if (Karst::MemorySafety::IsReadable(nameSlot))
						{
							const char* source = reinterpret_cast<const char*>(*nameSlot);
							if (source && Karst::MemorySafety::IsReadable(source, 1))
							{
								haveName = true;
								for (size_t i = 0; i + 1 < KARST_COUNTOF(name); ++i)
								{
									if (!Karst::MemorySafety::IsReadable(source + i, 1))
										break;
									name[i] = source[i];
									if (name[i] == '\0')
										break;
								}
								name[KARST_COUNTOF(name) - 1] = '\0';
							}
						}

						::swprintf_s(out, outCount, L"%p ref type=0x%04X %dx%d frames=%d file='%ls'",
							shp, type, width, height, frames,
							Widen(haveName ? name : "?").c_str());
					}
					else
					{
						::swprintf_s(out, outCount, L"%p data type=0x%04X %dx%d frames=%d",
							shp, type, width, height, frames);
					}
				}

				// Calls the engine's ART-section reload, isolating SEH from any
				// function that owns C++ objects (MSVC C2712). Returns false when the
				// engine raised - the caller only reports that.
				bool CallLoadArtSection(void* type, DWORD theater)
				{
					typedef void(__thiscall* LoadArtFn)(void* self, DWORD theater);
					LoadArtFn loadArt = reinterpret_cast<LoadArtFn>(
						EngineLayout::LoadArtSectionFunction);

					if (!loadArt)
						return false;

					__try
					{
						loadArt(type, theater);
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return false;
					}

					return true;
				}



				// One-line-per-field census of every SHP slot on a building type,
				// plus the answer GetImage() itself gives.
				//
				// This is what decides the open question in the handoff - "which
				// field does the draw path read". Instead of guessing a field, the
				// engine's own virtual GetImage() (BuildingClass vtable slot 30 ->
				// BuildingTypeClass 0x7E4570+0x9C -> 0x45F040) is called and its
				// result printed next to the fields it could plausibly come from.
				// Defined below; DumpTypeImages calls it to list the name strings.
				void DumpAnimNameTable(BuildingTypeClass* type, const char* when);







				// Country pointer offset, once discovered.
				// Owning-house offset and country offset, both discovered at runtime.
				int g_ownerOffset = 0;
				int g_countryOffset = 0;
				bool g_ownerOffsetReported = false;
				int g_ownerCandidatesLogged = 0;
				int g_ownerSearchAttempts = 0;

				// How many ART-load substitutions have been logged.
				int g_artHooksApplied = 0;
				int g_copiesPicked = 0;
				int g_deployTimingReports = 0;
				int g_createHookFires = 0;
				int g_allocArgsReports = 0;
				int g_clonePicksReported = 0;
				int g_cloneIdxReports = 0;
				int g_buildingTypeSwaps = 0;

				// How many buildings were created from a per-country type copy
				// (goal B). Bounded so the log stays readable.
				int g_ownerTypeSwaps = 0;

				// How many substitutions faulted inside an engine lookup and were
				// therefore skipped. A non-zero value here means SEH caught something
				// that would otherwise have been a fatal error.
				int g_ownerTypeFailures = 0;

				// The ART loader runs during very early startup as well, before
				// BuildingTypeClass::Array exists. Touching the array at that point
				// crashed the game on the loading screen. The hook therefore stays
				// inert until a match is actually running.
				bool g_GameRunning = false;
				bool g_countryOffsetReported = false;
				bool g_diagnosticsReported = false;

				// How many detailed sweeps have been logged, so the log stays short.
				int g_sweepsReported = 0;

				// The one-off list census is emitted once per process.
				bool g_arrayCensusDone = false;

				// Sweep bookkeeping: how many ran, when the first one ran, how many
				// buildings the last one saw, and whether the give-up fired.
				unsigned g_sweeps = 0;
				DWORD g_firstSweepTick = 0;
				int g_lastBuildingCount = 0;
				bool g_gaveUp = false;

				// Concatenated names of every building type seen so far, used to
				// detect first appearances. A live match only produces the player's
				// own buildings well after the map scenery, so "log on change" is
				// what catches them.
				std::string g_seenTypeNames;

				// How many selected-object traces are still allowed.
				int g_selectedTracesLeft = 12;



				unsigned DiscoverCountryOffset(const void* owner);








			}





			// (The old 0x45197B probe was removed: it never fired for GACNST.)



			// Defined below, next to the draw-time rule that also uses it.
			bool CountryListContains(const char* list, int countryIndex);








			// -------------------------------------------------------------------
			// Section redirect table  (single source of truth)
			// -------------------------------------------------------------------
			//
			// This list is used twice:
			//
			//   1. RedirectSectionBeforeFullParse() points a type's ART section at
			//      the target building's section, before the engine reads it. The
			//      engine then parses every key itself and creates the main image,
			//      the activity animations and the damaged/production ones.
			//
			//   2. ApplyFoundationFromSection() copies the target building's
			//      FOUNDATION onto the redirected type, so the footprint follows
			//      the art instead of staying at the original building's size.
			//      A footprint is a game rule (rulesmd.ini Foundation=), so art
			//      redirection alone can never move it - the value has to be taken
			//      from the target type, which is exactly what this does.
			//
			// Keep the two uses on the same table so they cannot drift apart.
			// The ART section name and the building-type ID are NOT always the
			// same, and the difference is easy to miss:
			//
			//   [GAWEAT]  (rulesmd)  Image=GAWETH
			//   [GAWETH]  (artmd)    Foundation=3x3, SuperAnim=GAWETH_E ...
			//
			// The type is registered as GAWEAT; GAWETH only exists in artmd. So the
			// ART redirect must use `section` (GAWETH, or the engine will parse
			// nothing), while the footprint must be read from the TYPE `targetType`
			// (GAWEAT, because GAWETH is not in BuildingTypeClass::Array at all).
			//
			// `targetType` may be empty, meaning "same as section".
			struct SectionRedirect
			{
				std::string typeId;      // the building being changed, e.g. "NAPOWR"
				std::string section;     // its ART section, e.g. "GAPOWR"
				std::string targetType;  // its type ID if that differs, else ""

				// A script asked for the look ONLY, leaving the footprint alone
				// ("foundationFrom": "self"). The INI form has no way to say this,
				// so it is false for every INI rule.
				//
				// Why it exists: following the target's footprint is usually what
				// you want, but the target's Foundation can have the same area and
				// a different shape (3x2 vs 2x3), and a script that only wanted a
				// different picture should not silently change where the building
				// may be placed.
				bool keepFoundation = false;

				// Which owning country this entry applies to.
				//
				//   -1  -> applies to every owner (the global `ShowByArt` form)
				//   >=0 -> applies only to buildings whose owner has this country
				//
				// The >=0 form is what `ShowByCountries`/`ShowBySHP` produce. It
				// cannot be applied by rewriting the shared type's ImageFile - the
				// type is shared by every player of that building - so those entries
				// are realised as per-country type copies and selected per building.
				int countryIndex = -1;
				std::string countryName;  // for logging only
			};

			// The redirect list, loaded from the INI at match start.
			//
			// This used to be a hard-coded table. It is now read from each
			// building's own section, so adding a building needs no rebuild:
			//
			//     [NAPOWR]
			//     ShowByArt=GAPOWR
			//
			//     [NAMISL]
			//     ShowByArt=GAWETH
			//     ShowByArtType=GAWEAT
			//
			// Empty means "nothing configured": no redirect, no footprint copy, and
			// the game behaves exactly as without this module.
			std::vector<SectionRedirect> g_sectionRedirects;

			// A stable view of the same entries for logging.
			int g_redirectCount = 0;

			// Declared here, in the SAME scope as its definition, because
			// RedirectSectionBeforeFullParse() (defined just below) is the function
			// that actually runs during map load and therefore has to build the list.
			//
			// A previous attempt put this declaration in a nested scope further up
			// the file; the compiler then treated it as a second overload and failed
			// with C2668 at the call site.
			void ParseSectionRedirects();



			// Called from the entry of the engine's FULL ART-section parser
			// (0x45FE50), before it reads ImageFile at 0x461608.
			//
			// THIS IS THE CORRECT PLACE TO REDIRECT A TYPE'S ART SECTION.
			//
			// The engine then parses the target section itself and fills every key it
			// knows - Image, Buildup, ActiveAnim, IdleAnim, ProductionAnim and the rest
			// - exactly as it does for the original section. Nothing is copied by hand,
			// and the animation objects it creates later are built from the engine's own
			// data, with the engine's own position and palette.
			//
			// The pairs come from g_sectionRedirects (see above), which is also what
			// ApplyFoundationFromSection() reads - so the art and the footprint can
			// never disagree about which building is being imitated.
			void RedirectSectionBeforeFullParse(void* pType)
			{
				// BUILD THE REDIRECT LIST HERE.
				//
				// This is the ONLY entry point of this module that actually runs
				// during map load. The list used to be read from
				// EnsureRulesForLoadTime(), which is called only by PrepareTypeArt()
				// - and PrepareTypeArt is the handler for 0x45F230, an address this
				// project deliberately does NOT hook (see dllmain.cpp: "ART section
				// loader (0x45F230) - NOT HOOKED, and why"). That made the whole
				// read path dead code, so g_sectionRedirects stayed empty for the
				// entire match and every ShowByArt entry was silently ignored.
				//
				// The hard-coded version of this feature appeared to work only
				// because it needed no parsing at all.
				//
				// First call wins: by the time the engine parses a building type's
				// ART section, the rules INI is already populated (the type itself
				// was built from it), which is exactly the precondition the read
				// needs.
				//
				// Retries are allowed while the list is still empty, because latching
				// an empty result is precisely how this feature was silently disabled
				// before: an empty list is indistinguishable from "not configured
				// yet", and the cost of a few extra scans at load time is nothing
				// next to losing the configuration for a whole match.
				if (!g_redirectsParsed)
				{
					static int parseAttempts = 0;

					if (parseAttempts < 5)
					{
						++parseAttempts;
						ParseSectionRedirects();

						// Latch only on a non-empty result, or once the retries are
						// exhausted - so "genuinely nothing configured" still stops
						// the work.
						if (g_redirectCount > 0 || parseAttempts >= 5)
							g_redirectsParsed = true;
					}
					else
					{
						g_redirectsParsed = true;
					}
				}

				BuildingTypeClass* type = reinterpret_cast<BuildingTypeClass*>(pType);

				if (!type || !Karst::MemorySafety::IsReadable(type))
					return;

				const char* id = type->ID;
				if (!id)
					return;

				for (const SectionRedirect& redirect : g_sectionRedirects)
				{
					// Per-country entries are NOT applied to the shared type: the
					// type is used by every player of that building, so pointing it
					// at one country's art would change it for everyone. Those are
					// realised as per-country type copies instead (CreateCountryCopies
					// + the PickTypeForOwner hook).
					if (redirect.countryIndex >= 0)
						continue;

					if (_stricmp(id, redirect.typeId.c_str()) != 0)
						continue;

					if (type->ImageFile[0] != '\0'
						&& _stricmp(type->ImageFile, redirect.section.c_str()) == 0)
					{
						return;
					}

					strncpy_s(type->ImageFile, redirect.section.c_str(), _TRUNCATE);

					Karst::LogInfo(L"ShowByCountry[parse] %ls: ART section set to '%ls' "
						L"before the engine reads it.",
						Widen(id).c_str(),
						Widen(redirect.section.c_str()).c_str());

					return;
				}
			}

			// Reads the redirect list out of the INI.
			//
			// Replaces what used to be a hard-coded table. Each building declares its
			// own redirect in its own section:
			//
			//     [NAPOWR]
			//     ShowByArt=GAPOWR            ; look like the Allied power plant
			//
			//     [NAMISL]
			//     ShowByArt=GAWETH            ; art section ...
			//     ShowByArtType=GAWEAT        ; ... and the registered type, when they
			//                                 ;     differ (GAWETH exists only in artmd)
			//
			// Read from rulesmd.ini first, then artmd.ini, mirroring how the existing
			// ShowByCountries/ShowBySHP keys are looked up: a mod author writes
			// building keys in the building's rules section, but art-side placement is
			// accepted too.
			//
			// Called once when the match starts. Safe to call again (it rebuilds the
			// list from scratch).
			void ParseSectionRedirects()
			{
				g_sectionRedirects.clear();
				g_redirectCount = 0;

				auto* buildingTypes = Karst::MemorySafety::SafeReadSlot<
					DynamicVectorClass<BuildingTypeClass*>*>(&BuildingTypeClass::Array);

				CCINIClass* rulesINI = Karst::MemorySafety::SafeReadSlot<CCINIClass*>(
					&CCINIClass::INI_Rules);

				CCINIClass* artINI = Karst::MemorySafety::SafeReadSlot<CCINIClass*>(
					&CCINIClass::INI_Art);

				if (!buildingTypes || buildingTypes->Count <= 0
					|| (!rulesINI && !artINI))
				{
					Karst::LogDebug(L"ShowByCountry[redirect]: INI or type list not ready yet.");
					return;
				}

				const CCINIClass* sources[2] = { rulesINI, artINI };

				// -----------------------------------------------------------
				// Scripts first.
				//
				// A script that gives a building a single `look` is exactly the
				// global ShowByArt form, so it is turned into the same kind of
				// redirect. Scripts win over the INI: a building a script mentions
				// is not looked up in the INI at all, which is what makes a
				// conflict diagnosable by reading the script alone.
				//
				// A script that uses `byCountry` is NOT handled here - that is
				// CountryArt's job, because one shared type cannot carry a
				// per-owner section name.
				// -----------------------------------------------------------
				int fromScripts = 0;

				for (const Karst::Script::AppearanceRule& rule :
					KarstExtension::GetScriptAppearanceRules())
				{
					if (!rule.Look.empty())
					{
						SectionRedirect redirect;
						redirect.typeId = rule.Building;
						redirect.section = rule.Look;
						redirect.targetType = rule.FoundationFrom;
						redirect.keepFoundation = rule.KeepFoundation;
						redirect.countryIndex = -1;

						g_sectionRedirects.push_back(std::move(redirect));
						++fromScripts;
						continue;
					}

					// Per-country: leave it to CountryArt, but make sure the INI is
					// not allowed to override it either.
					if (!rule.ByCountry.empty())
						++fromScripts;
				}

				if (fromScripts > 0)
				{
					Karst::LogInfo(L"ShowByCountry[redirect]: %d script rule(s) "
						L"applied ahead of the INI.", fromScripts);
				}

				// True when a script already decided this building's art, either
				// form; the INI must then leave it alone.
				const auto handledByScript =
					[](const char* id) -> bool
					{
						if (!id)
							return false;

						for (const Karst::Script::AppearanceRule& rule :
							KarstExtension::GetScriptAppearanceRules())
						{
							if (::_stricmp(rule.Building.c_str(), id) == 0)
								return true;
						}

						return false;
					};

				// Diagnostics for the whole call: how many types were scanned and how
				// many declared ShowByArt. Without this, "the list is empty because
				// nothing is configured" and "the list is empty because the INI was
				// not readable yet" look identical in the log - and that ambiguity
				// already cost one silent regression.
				int scanned = 0;
				int declared = 0;

				for (int i = 0; i < buildingTypes->Count; ++i)
				{
					BuildingTypeClass* type = buildingTypes->Items[i];

					if (!type || !Karst::MemorySafety::IsReadable(type) || !type->ID)
						continue;

					++scanned;

					// A script already decided this building; the INI does not get
					// a second say.
					if (handledByScript(type->ID))
						continue;

					// The section to read is the building's own section. `Image=` is
					// deliberately NOT used here: the configuration belongs to the
					// building, and using the art section would make the key follow
					// whatever the art points at.
					const char* section = type->ID;
					const int typeIndex = i;

					std::string artName;
					std::string typeName;
					bool found = false;

					for (const CCINIClass* source : sources)
					{
						if (!source)
							continue;

						if (!found && ReadKey(const_cast<CCINIClass*>(source), section,
							KEYS_ART, KARST_COUNTOF(KEYS_ART), artName))
						{
							found = true;
						}

						if (typeName.empty())
						{
							ReadKey(const_cast<CCINIClass*>(source), section,
								KEYS_ART_TYPE, KARST_COUNTOF(KEYS_ART_TYPE), typeName);
						}
					}

					if (found)
						++declared;

					if (found)
					{
						SectionRedirect redirect;
						redirect.typeId = type->ID;
						redirect.section = artName;

						// When the art section is not also a registered type, the caller
						// supplies the type explicitly. Otherwise they are the same name
						// and the footprint can be read straight from `section`.
						redirect.targetType = typeName;
						redirect.countryIndex = -1;

						g_sectionRedirects.push_back(std::move(redirect));
					}

					// The per-country form (ShowByCountries / ShowBySHP) used to be
					// read here: it produced one entry per (building, country) pair,
					// which then had to be realised as per-country type clones because
					// the shared type cannot carry a per-owner section name.
					//
					// That whole path is REMOVED. The clones were built as
					// `memcpy(source) + ImageFile=<target section>`, so they paired the
					// target section's art with the SOURCE type's animation names -
					// which is what made the activity animations wrong - and the two
					// runtime mechanisms that consumed them (a construction-time Type
					// swap and a per-frame draw-time swap) duplicated each other.
					//
					// What remains is the global ShowByArt form above: rename the
					// type's ART section before the engine parses it, so the engine
					// fills every key - image, animations and all - from the target
					// section. No clone, no draw hook, nothing per-owner.
				}

				g_redirectCount = static_cast<int>(g_sectionRedirects.size());

				Karst::LogInfo(L"ShowByCountry[redirect]: scanned %d type(s), %d declare "
					L"ShowByArt, %d redirect entr(ies) in total (rules=%s art=%s).",
					scanned, declared, g_redirectCount,
					rulesINI ? L"yes" : L"no", artINI ? L"yes" : L"no");

				if (g_redirectCount == 0)
				{
					Karst::LogInfo(L"ShowByCountry: no building declares ShowByArt - "
						L"the global redirect stays out of the way.");
					return;
				}

				Karst::LogInfo(L"ShowByCountry: %d building(s) redirect their art "
					L"(ShowByArt).", g_redirectCount);

				for (const SectionRedirect& redirect : g_sectionRedirects)
				{
					const std::wstring wideType = Widen(redirect.typeId.c_str());
					const std::wstring wideSection = Widen(redirect.section.c_str());

					if (redirect.targetType.empty())
					{
						Karst::LogInfo(L"ShowByCountry[redirect]: %ls -> art section '%ls'",
							wideType.c_str(), wideSection.c_str());
					}
					else
					{
						Karst::LogInfo(L"ShowByCountry[redirect]: %ls -> art section '%ls', "
							L"footprint type '%ls'",
							wideType.c_str(), wideSection.c_str(),
							Widen(redirect.targetType.c_str()).c_str());
					}
				}
			}

			// Copies the imitated building's FOUNDATION onto the redirected type, so
			// the footprint follows the art.
			//
			// WHY THIS IS NEEDED
			//
			// Pointing a type at another building's ART section changes how it LOOKS
			// but not how much ground it occupies: the footprint is a game rule and
			// comes from rulesmd.ini (`Foundation=`), which this module never touches.
			// So a Soviet power plant showing Allied art still claimed 3x2 while the
			// Allied power plant it imitates is 2x2 - the user's report, and correct
			// behaviour for the engine.
			//
			// WHY COPYING FROM THE TYPE IS EXACTLY RIGHT
			//
			// `Foundation=` does not stay a string: the engine resolves it at parse
			// time into an INDEX on the type (0x46122C -> 0x474DA0 -> [type + 0xEF0]),
			// and that index is what every placement and occupancy check reads.
			// The imitated building's type therefore already carries the correct
			// index. Copying that one DWORD gives the redirected type the imitated
			// building's footprint, with no occupancy maths and no hot-path hook.
			//
			// The three fields travel together - the index plus its two companions -
			// because that is what the parser sets as a group (0x461248/0x461251):
			//
			//     +0xEE8   layout index        (0x4610A0)
			//     +0xEF0   Foundation index    (0x46122C)
			//     +0xEF4   companion value     (0x4610D8)
			//
			// READ-ONLY EXCEPT for those three fields on the redirected type.
			void ApplyFoundationFromSection(const char* when)
			{
				// The engine's own type list; the target is looked up by name in it,
				// so the index we copy is guaranteed to be one the engine published.
				auto* types = Karst::MemorySafety::SafeReadSlot<
					DynamicVectorClass<BuildingTypeClass*>*>(&BuildingTypeClass::Array);

				if (!types || types->Count <= 0)
				{
					Karst::LogDebug(L"ShowByCountry[foundation]: type list not ready (%ls).",
						Widen(when).c_str());
					return;
				}

				struct Alias
				{
					const char* target;   // the imitated building, e.g. "GAPOWR"
					const char* field;    // what was copied, for the log
				};

				int applied = 0;

				for (const SectionRedirect& redirect : g_sectionRedirects)
				{
					// Per-country entries get their footprint from the same copy that
					// carries their art, i.e. on the per-country type copy, not here.
					if (redirect.countryIndex >= 0)
						continue;

					// A script asked for the look only; the ART redirect above still
					// applies, the building just keeps its own placement.
					if (redirect.keepFoundation)
					{
						Karst::LogInfo(L"ShowByCountry[foundation]: %ls keeps its own "
							L"footprint (script asked for the look only).",
							Widen(redirect.typeId.c_str()).c_str());
						continue;
					}

					BuildingTypeClass* source = nullptr;
					BuildingTypeClass* target = nullptr;

					// The footprint must come from a registered TYPE. For most
					// entries that is the section name; for the weather controller
					// the section is GAWETH but the type is GAWEAT.
					const std::string targetTypeId = redirect.targetType.empty()
						? redirect.section : redirect.targetType;

					for (int i = 0; i < types->Count; ++i)
					{
						BuildingTypeClass* candidate = types->Items[i];

						if (!candidate || !Karst::MemorySafety::IsReadable(candidate)
							|| !candidate->ID)
						{
							continue;
						}

						if (!source && _stricmp(candidate->ID, redirect.typeId.c_str()) == 0)
							source = candidate;

						if (!target && _stricmp(candidate->ID, targetTypeId.c_str()) == 0)
							target = candidate;
					}

					if (!source || !target)
					{
						Karst::LogDebug(L"ShowByCountry[foundation]: %ls -> %ls: "
							L"type not found (source=%ls target=%ls).",
							Widen(redirect.typeId.c_str()).c_str(),
							Widen(redirect.section.c_str()).c_str(),
							source ? L"yes" : L"no", target ? L"yes" : L"no");
						continue;
					}

					const BYTE* targetRaw = reinterpret_cast<const BYTE*>(target);
					BYTE* sourceRaw = reinterpret_cast<BYTE*>(source);

					// Only redirect types whose ART actually points at the target,
					// i.e. the ones this feature changed. Anything else is left alone.
					if (_stricmp(source->ImageFile, redirect.section.c_str()) != 0)
					{
						Karst::LogDebug(L"ShowByCountry[foundation]: %ls is not "
							L"redirected to %ls yet (ImageFile='%ls') - skipped.",
							Widen(redirect.typeId.c_str()).c_str(),
							Widen(redirect.section.c_str()).c_str(),
							Widen(source->ImageFile).c_str());
						continue;
					}

					// Old values first, so the log can show a real change.
					const LONG oldLayout = *reinterpret_cast<const LONG*>(
						sourceRaw + 0xEE8);
					const LONG oldFoundation = *reinterpret_cast<const LONG*>(
						sourceRaw + 0xEF0);
					const LONG oldExtra = *reinterpret_cast<const LONG*>(
						sourceRaw + 0xEF4);
					const LONG oldGridA = *reinterpret_cast<const LONG*>(
						sourceRaw + 0xDFC);
					const LONG oldGridB = *reinterpret_cast<const LONG*>(
						sourceRaw + 0xED4);

					const LONG newLayout = *reinterpret_cast<const LONG*>(
						targetRaw + 0xEE8);
					const LONG newFoundation = *reinterpret_cast<const LONG*>(
						targetRaw + 0xEF0);
					const LONG newExtra = *reinterpret_cast<const LONG*>(
						targetRaw + 0xEF4);
					const LONG newGridA = *reinterpret_cast<const LONG*>(
						targetRaw + 0xDFC);
					const LONG newGridB = *reinterpret_cast<const LONG*>(
						targetRaw + 0xED4);

					if (oldLayout == newLayout && oldFoundation == newFoundation
						&& oldExtra == newExtra && oldGridA == newGridA
						&& oldGridB == newGridB)
					{
						continue;
					}

					*reinterpret_cast<LONG*>(sourceRaw + 0xEE8) = newLayout;
					*reinterpret_cast<LONG*>(sourceRaw + 0xEF0) = newFoundation;
					*reinterpret_cast<LONG*>(sourceRaw + 0xEF4) = newExtra;

					// +0xDFC and +0xED4 are the PRECOMPUTED pointers derived from the
					// Foundation index at type-init time (0x4651FB -> 0x89C900 /
					// 0x89D368 tables, 88 bytes per entry). Changing only the index
					// leaves these pointing at the ORIGINAL building's grid data,
					// which is exactly why the footprint did not move: a live test
					// with only +0xEF0 copied showed `foundation 5 -> 3` in the log
					// while the building still occupied 3x2 on screen.
					//
					// They are pointers into engine-owned static tables, so the
					// target's pointers are always valid to share - copying them is
					// the same thing the engine's own parser does for a section that
					// declares this Foundation.
					*reinterpret_cast<LONG*>(sourceRaw + 0xDFC) = newGridA;
					*reinterpret_cast<LONG*>(sourceRaw + 0xED4) = newGridB;

					++applied;

					Karst::LogInfo(L"ShowByCountry[foundation]: %ls now uses %ls's "
						L"footprint (%ls): layout %d->%d, foundation %d->%d, "
						L"extra %d->%d, gridA %d->%d, gridB %d->%d.",
						Widen(redirect.typeId.c_str()).c_str(),
						Widen(redirect.section.c_str()).c_str(),
						Widen(when).c_str(),
						oldLayout, newLayout,
						oldFoundation, newFoundation,
						oldExtra, newExtra,
						oldGridA, newGridA, oldGridB, newGridB);
				}

				if (applied > 0)
				{
					Karst::LogInfo(L"ShowByCountry[foundation]: %d type(s) took the "
						L"imitated building's footprint (%ls).", applied,
						Widen(when).c_str());
				}
			}







			// Called from the hook on the engine's ART-section loader (0x45F230),
			// with `pType` being the BuildingTypeClass about to be loaded.
			//
			// This is the only point where the section name cannot be undone: the
			// loader reads `Image=` and, when that key is absent, resets ImageFile to
			// the type's default name. Writing it from the main loop was therefore
			// overwritten on the next load, which is why the building kept drawing
			// its original animations even though the log said the rule was applied.
			//
			// The owning country of a specific building is not available here - the
			// loader works on the shared type - so the country comes from the local
			// player. Every building of that type then uses the local player's
			// appearance, which is consistent from that player's point of view.
			// -------------------------------------------------------------------
			// Country-specific type copies  (goal B)
			// -------------------------------------------------------------------
			//
			// The engine resolves a building's look from its BuildingTypeClass, and
			// that class is SHARED by every player of that type. There is therefore
			// no place on it to store "this country looks different": whatever is
			// written there applies to everyone.
			//
			// The original game solves country differences by having separate types
			// (GACNST / NACNST / YACNST). This module does the same thing at runtime:
			// for each (type, country) pair it clones the type, points the clone at
			// the country's ART section, and hooks BuildingTypeClass::CreateObject so
			// that a building is created from the clone matching its owner's country.
			//
			// The clone is what makes the feature work: its Image and its animation
			// names both come from the country's own ART section, and being a
			// distinct object, it cannot be overwritten by another country.








			// BuildingTypeClass::Array is static DynamicVectorClass<...>* const.
			// Naming the concrete type once keeps every access below readable, and
			// avoids the template-deduction trouble that &Array (type T* const*)
			// causes for the generic slot reader.
			typedef DynamicVectorClass<BuildingTypeClass*> TypeList;
			typedef TypeList* TypeListPtr;


			// Marks the moment the feature may act, and applies the footprint copy.
			//
			// This used to do two more things, both removed with the per-country
			// feature: ApplyLocalCountryArt() bound the LOCAL player's art to every
			// configured type (plan A), and CreateCountryCopies() built the
			// per-country type clones (plan B). Neither is needed now: the global
			// ShowByArt path renames each type's ART section before the engine parses
			// it, which needs no clone and no draw hook.
			void MarkGameRunning()
			{
				if (g_GameRunning)
					return;

				g_GameRunning = true;
				Karst::LogDebug(L"ShowByCountry: feature is active.");

				// Give the redirected types the footprint of the building they
				// imitate. Done here rather than at parse time because rulesmd.ini
				// (where Foundation lives) is loaded around the same time as the ART
				// sections, and this is the first point where both are present.
				ApplyFoundationFromSection("match start");
			}









			// Reports that the engine's sidebar-MIX function was entered at all.
			//
			// WHY THIS EXISTS
			//
			// The first probe run produced no output, which is ambiguous: either the
			// engine never called 0x534FA0 in that flow (so some other path chooses
			// the sidebar MIX), or it did call it and Ares' hook does not return to
			// 0x534FBB as its published source suggests. One line here separates the
			// two answers.
			//
			// ECX is the side index the caller passed.
			//
			// WRITES NOTHING.
			void ProbeSidebarMixEntry(unsigned long sideIndex)
			{
				static int reportsLeft = 8;
				if (reportsLeft <= 0)
					return;

				--reportsLeft;

				Karst::LogInfo(L"ShowByCountry[mixentry] 0x534FA0 entered: "
					L"sideIndex(ECX)=%u", sideIndex);
			}

			// -------------------------------------------------------------------
			// READ-ONLY probe: what identifies the player's COUNTRY at the moment
			// the engine picks its sidebar MIX?
			// -------------------------------------------------------------------
			//
			// GOAL
			//
			// Ares' `Sidebar.MixFileIndex` works per SIDE (faction). The user wants
			// it per COUNTRY, so Karst needs to know the player's country at the
			// instant the engine decides which SIDECnnMD.MIX to load.
			//
			// WHAT THE ENGINE DOES (measured in gamemd 1.001)
			//
			//   0x0067E74A  mov  eax, [0xa8b230]       ; ScenarioClass::Instance
			//   0x0067E75B  mov  ecx, [eax + 0x34B8]   ; PlayerSideIndex  <- SIDE, not country
			//   0x0067E761  call 0x534FA0              ; builds "SIDEC%02dMD.MIX"
			//
			//   0x00534FB1  cmp  esi, 2                ; <- Ares hooks HERE (5 bytes)
			//   0x00534FBB  mov  ebx, esi              ; <- Karst would hook HERE (2 bytes)
			//
			//      Ares: 0x534FB1..0x534FB5
			//      Karst: 0x534FBB..0x534FBC   (adjacent, no overlap -> they coexist)
			//
			// The engine only ever hands this function the SIDE index, so a
			// per-country choice has to come from somewhere Karst can read at that
			// instant. Three candidates exist and only one can be right, so this
			// probe reports all of them instead of guessing:
			//
			//   1. ScenarioClass::Instance + 0x34B8        -> known: PlayerSideIndex
			//   2. ScenarioClass::HumanPlayerHouseTypeIndex -> country index, offset UNKNOWN
			//   3. HouseClass::Player (0xA83D4C) -> Type    -> country, timing UNKNOWN
			//
			// HOW TO READ THE RESULT
			//
			// Run a match twice with DIFFERENT countries and compare the two
			// `[mixprobe]` blocks: the offset whose value follows the country (and
			// equals the country index from [Countries]) is the one to use.
			//
			// WRITES NOTHING.
			void ProbeSidebarMixContext(unsigned long sideIndex)
			{
				static int reportsLeft = 24;
				if (reportsLeft <= 0)
					return;

				--reportsLeft;

				auto readDword = [](const void* base, unsigned offset) -> LONG
				{
					const DWORD* p = reinterpret_cast<const DWORD*>(
						static_cast<const BYTE*>(base) + offset);

					return Karst::MemorySafety::IsReadable(p, 4)
						? static_cast<LONG>(*p) : -1;
				};

				// ScenarioClass::Instance lives at a fixed address; reading the
				// global through the safe reader rather than trusting [addr] keeps
				// this harmless if it is not up yet.
				const void* scenario = *reinterpret_cast<const void* const*>(0x00A8B230);
				const void* player = *reinterpret_cast<const void* const*>(0x00A83D4C);

				Karst::LogInfo(L"ShowByCountry[mixprobe] call: sideIndex(ESI)=%u "
					L"scenario=%p player=%p",
					sideIndex, scenario, player);

				if (scenario && Karst::MemorySafety::IsReadable(scenario, 0x3600))
				{
					Karst::LogInfo(L"ShowByCountry[mixprobe]   Scenario+0x34B8 "
						L"(PlayerSideIndex) = %d", readDword(scenario, 0x34B8));

					// WHERE HumanPlayerHouseTypeIndex MUST BE
					//
					// ScenarioClass lays out (YRpp):
					//
					//     int ThemeIndex;
					//     int HumanPlayerHouseTypeIndex;    <- the one we want
					//     double CarryOverMoney; int CarryOverCap; int Percent;
					//     Variable GlobalVariables [50];
					//     Variable LocalVariables [100];
					//     CellStruct View1..View4;
					//     DWORD unknown_34A0; bool FreeRadar;   // FreeRadar = 0x34A4
					//     ... int PlayerSideIndex;              // = 0x34B8
					//
					// With sizeof(Variable) == 0x10 that gives
					//     0x34A4 (FreeRadar) - 4 (unknown_34A0) - 4*4 (views)
					//       - 100*0x10 - 50*0x10 = 0x2B1C
					// so the field sits near 0x2B1C, NOT in the 0x3490+ region
					// that the previous revision scanned (which is why it was not
					// in the output at all).
					//
					// Both regions are swept now: the second only to keep the
					// previously observed values comparable.
					wchar_t list[900] = { 0 };
					wchar_t item[40] = { 0 };
					int shown = 0;

					const unsigned ranges[2][2] = {
						{ 0x2A00, 0x2C00 },   // expected HumanPlayerHouseTypeIndex
						{ 0x3490, 0x3600 },   // previously scanned region
					};

					for (int r = 0; r < 2 && shown < 32; ++r)
					{
						for (unsigned offset = ranges[r][0];
							offset < ranges[r][1] && shown < 32; offset += 4)
						{
							const LONG value = readDword(scenario, offset);

							if (value < 0 || value > 40)
								continue;

							::_snwprintf_s(item, _TRUNCATE, L"+%04X=%d ",
								offset, value);
							wcscat_s(list, item);
							++shown;
						}
					}

					Karst::LogInfo(L"ShowByCountry[mixprobe]   Scenario candidates "
						L"(0..40): %ls", list[0] ? list : L"(none)");
				}

				if (player && Karst::MemorySafety::IsReadable(player, 0x400))
				{
					// HouseClass::Player is a live HouseClass. Sweep its early
					// members for something that looks like a HouseTypeClass*:
					// a pointer into the engine image. Reported as raw addresses so
					// they can be resolved by hand against HouseTypeClass::Array.
					//
					// The window is 0x400 because YRpp places `HouseTypeClass* Type`
					// a fair way into the class; the earlier 0x100 window found
					// nothing at all.
					wchar_t list[900] = { 0 };
					wchar_t item[48] = { 0 };
					int shown = 0;

					for (unsigned offset = 0; offset < 0x400 && shown < 16; offset += 4)
					{
						const LONG value = readDword(player, offset);

						if (value < 0x400000 || value > 0x7E0000)
							continue;

						::_snwprintf_s(item, _TRUNCATE, L"+%03X=%08X ", offset, value);
						wcscat_s(list, item);
						++shown;
					}

					Karst::LogInfo(L"ShowByCountry[mixprobe]   Player HouseClass "
						L"pointers: %ls", list[0] ? list : L"(none)");
				}
			}

			// -------------------------------------------------------------------
			// Per-COUNTRY sidebar MIX  (Sidebar.MixFileIndex, country granularity)
			// -------------------------------------------------------------------
			//
			// Ares' `Sidebar.MixFileIndex` chooses SIDECnnMD.MIX per SIDE, because
			// the engine only ever hands the SIDE index to the code that builds the
			// name. The country is available at the moment the player commits to a
			// match, but the engine discards it long before the file name is built.
			//
			// WHERE THE COUNTRY COMES FROM
			//
			// NOT `[Basic] Player=`: in a skirmish that key holds the PLAYER NAME,
			// not a country, so resolving it through FindIndexByName returns -1 and
			// the engine takes its "unknown" branch. (Hooking that branch was tried
			// and never fired - that is how this was found.)
			//
			// The country the player picked in the game-setup screen lives in the
			// session's start spot:
			//
			//     SessionClass::Instance (0xA8B238)
			//         -> StartSpots (DynamicVectorClass<NodeNameType*>)
			//             -> Items[0]->Country          <- country index
			//
			// which is exactly what Ares reads for its own per-side UI work.
			//
			// WHERE IT IS APPLIED
			//
			// 0x5350D3, the convergence point where the side index becomes the file
			// name:
			//
			//   0x005350D3  lea  esi, [ebx + 1]    ; side index + 1
			//   0x005350DB  push 0x827e0c          ; "SIDEC%02dMD.MIX"
			//   0x005350E1  call 0x7c8ef4          ; sprintf
			//
			// Writing EBX there makes the engine build a different SIDECnnMD.MIX
			// name, while everything downstream (loading, the sidebar layout, the
			// cameos) stays the engine's own work. Nothing is drawn or copied here.
			//
			// Unlike the Ares hook site (0x534FB1), this address is not used by
			// Ares, so both features can be installed at once: Ares decides by side,
			// this overrides by country when the country has a rule.

			// Reads the country index the player selected in the game-setup screen.
			// Returns -1 when there is no session yet (main menu, loading screen).
			int SelectedCountryIndex()
			{
				__try
				{
					SessionClass* session = SessionClass::Instance;

					if (!session || session->StartSpots.Count <= 0)
						return -1;

					NodeNameType* spot = session->StartSpots.Items[0];

					return spot ? spot->Country : -1;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					return -1;
				}
			}

			// Looks up `Sidebar.MixFileIndex` for the country the player picked.
			//
			// Returns true and fills `mixIndex` when the country declares the key.
			// A country without the key is left completely alone, so this cannot
			// change anything for mods that do not use it.
			bool CountrySidebarMixIndex(int& mixIndex)
			{
				const int countryIndex = SelectedCountryIndex();

				if (countryIndex < 0)
					return false;

				// Country index -> ID, which is the INI section to read.
				const char* section = nullptr;

				__try
				{
					if (HouseTypeClass::Array
						&& countryIndex < HouseTypeClass::Array->Count)
					{
						HouseTypeClass* houseType =
							HouseTypeClass::Array->Items[countryIndex];

						if (houseType && houseType->ID
							&& Karst::MemorySafety::IsReadable(houseType->ID, 1))
						{
							section = houseType->ID;
						}
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					section = nullptr;
				}

				if (!section)
					return false;

				CCINIClass* ini = CCINIClass::INI_Rules;

				if (!ini)
					return false;

				// 1-based, same as Ares: 1 == SIDEC01MD.MIX.
				const int value = ini->ReadInteger(section, "Sidebar.MixFileIndex", -1);

				if (value < 1)
					return false;

				mixIndex = value;

				Karst::LogInfo(L"ShowByCountry[mixcountry] '%hs' (index %d) -> "
					L"Sidebar.MixFileIndex=%d", section, countryIndex, value);

				return true;
			}

			// Drops the cached ShowByArt list so a new match re-reads the INI.
			//
			// It used to clear a large amount of per-country state (rule matches,
			// resolved owner/country offsets, the sweep counters, the list of
			// already-reported types). All of that went away with the per-country
			// feature, so what remains is the list and the two flags that guard it.
			void ResetForNewMatch()
			{
				g_sectionRedirects.clear();
				g_redirectCount = 0;
				g_redirectsParsed = false;
			}
		}
	}
}
