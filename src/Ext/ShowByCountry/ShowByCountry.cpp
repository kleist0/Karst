#include <Ext/ShowByCountry/ShowByCountry.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Karst.h>

// GameClasses.h pulls the engine classes in dependency order; including
// BuildingTypeClass.h on its own leaves base classes incomplete.
#include <GameClasses.h>
#include <ObjectClass.h>
#include <TechnoClass.h>
#include <BuildingTypeClass.h>
#include <HouseTypeClass.h>
#include <CCINIClass.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
			// Nothing is parsed here: the redirect needs the rules INI, which only
			// exists once the engine starts parsing building ART. That is
			// RedirectSectionBeforeFullParse(), and it is where the list is read.
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

			namespace
			{
				// Narrows->wide for the log. The narrow text is always converted here
				// and passed with `%ls`: every message is formatted with the wide
				// `_vsnwprintf_s`, and a narrow string handed to it directly came out
				// as mojibake ("ImageFile='NACNST'" printed as CJK characters).
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
			}

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
			//
			// (This struct once also carried `countryIndex` and `keepFoundation`.
			// Both are gone: the per-country form is Ext\CountryArt now, and the
			// script bridge that could ask for "the look only" was removed.)
			struct SectionRedirect
			{
				std::string typeId;      // the building being changed, e.g. "NAPOWR"
				std::string section;     // its ART section, e.g. "GAPOWR"
				std::string targetType;  // its type ID if that differs, else ""
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

			// -------------------------------------------------------------------
			// Per-match footprint copy
			// -------------------------------------------------------------------
			//
			// The engine rebuilds every building type object when a new match starts
			// (measured for CountryArt: the same GACNST lives at 0x16A370D0 in match 1
			// and 0x16E33040 in match 2). Everything written onto a type therefore
			// belongs to ONE match, so the footprint copy has to be re-run for each of
			// them. The ART redirect does not: it happens again on every parse.
			//
			// Applying the copy once per PROCESS - what this module used to do - left
			// the second match onwards with the building's own Foundation: the art was
			// right and the ground it claimed was not, which is the least visible
			// failure this feature can have.

			namespace
			{
				bool g_GameRunning = false;

				// Identity of the CURRENT building-type list. A change means the engine
				// threw the previous type objects away, i.e. a new match.
				uintptr_t g_typeListSignature = 0;

				// How many more frames the footprint copy may be retried. The new type
				// objects are parsed during the load, and the imitated type's Foundation
				// index is only final once ITS parse has run, so a single attempt may
				// legitimately see "not redirected yet".
				int g_foundationPendingFrames = 0;

				// How many "not redirected yet" lines may still be logged for the current
				// list, so the retry loop cannot flood karst.log.
				int g_foundationSkipLogs = 0;

				// Hashes the two ends of the array rather than walking it: both move
				// when the engine rebuilds the list, and this runs once per frame.
				uintptr_t TypeListSignature()
				{
					auto* types = Karst::MemorySafety::SafeReadSlot<
						DynamicVectorClass<BuildingTypeClass*>*>(
							&BuildingTypeClass::Array);

					if (!types || types->Count <= 0)
						return 0;

					const uintptr_t first = reinterpret_cast<uintptr_t>(types->Items[0]);
					const uintptr_t last = reinterpret_cast<uintptr_t>(
						types->Items[types->Count - 1]);

					return first ^ (last << 1) ^ (static_cast<uintptr_t>(types->Count) << 3);
				}

				void EnsureFoundationForTypeList()
				{
					if (g_sectionRedirects.empty())
						return;

					const uintptr_t signature = TypeListSignature();

					if (signature == 0)
						return;

					if (signature != g_typeListSignature)
					{
						g_typeListSignature = signature;
						g_foundationPendingFrames = 300;   // ~5 s at 60 fps
						g_foundationSkipLogs = 5;

						Karst::LogDebug(L"ShowByCountry[foundation]: the engine published "
							L"a new building-type list - re-applying the footprint for this "
							L"match.");
					}

					if (g_foundationPendingFrames <= 0)
						return;

					--g_foundationPendingFrames;

					ApplyFoundationFromSection("new type list");
				}
			}

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
			// Read from rulesmd.ini first, then artmd.ini: a mod author writes
			// building keys in the building's rules section, but art-side placement is
			// accepted too.
			//
			// Called once per process, and again whenever the redirect gate has to
			// retry. Safe to call repeatedly (it rebuilds the list from scratch).
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

						g_sectionRedirects.push_back(std::move(redirect));
					}
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
						if (g_foundationSkipLogs > 0)
						{
							--g_foundationSkipLogs;

							Karst::LogDebug(L"ShowByCountry[foundation]: %ls -> %ls: "
								L"type not found (source=%ls target=%ls).",
								Widen(redirect.typeId.c_str()).c_str(),
								Widen(redirect.section.c_str()).c_str(),
								source ? L"yes" : L"no", target ? L"yes" : L"no");
						}

						continue;
					}

					const BYTE* targetRaw = reinterpret_cast<const BYTE*>(target);
					BYTE* sourceRaw = reinterpret_cast<BYTE*>(source);

					// Only redirect types whose ART actually points at the target,
					// i.e. the ones this feature changed. Anything else is left alone.
					//
					// This is the branch the per-frame retry lands in while the engine is
					// still loading, so it is logged a bounded number of times per type
					// list instead of once per frame.
					if (_stricmp(source->ImageFile, redirect.section.c_str()) != 0)
					{
						if (g_foundationSkipLogs > 0)
						{
							--g_foundationSkipLogs;

							Karst::LogDebug(L"ShowByCountry[foundation]: %ls is not "
								L"redirected to %ls yet (ImageFile='%ls') - skipped.",
								Widen(redirect.typeId.c_str()).c_str(),
								Widen(redirect.section.c_str()).c_str(),
								Widen(source->ImageFile).c_str());
						}

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







			// Marks the moment the feature may act, and keeps the footprint copy in
			// step with the engine's CURRENT type list - i.e. with the current match.
			//
			// Called every frame from the main loop. It used to do two more things,
			// both removed with the per-country feature: bind the LOCAL player's art
			// to every configured type (plan A), and build per-country type clones
			// (plan B). The global ShowByArt path needs neither.








			void MarkGameRunning()
			{
				if (!g_GameRunning)
				{
					g_GameRunning = true;
					Karst::LogDebug(L"ShowByCountry: feature is active.");
				}

				EnsureFoundationForTypeList();
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
			// Logged at DEBUG: this answers "did the engine take this path at all",
			// which is only worth asking while diagnosing the per-country MIX
			// feature. At the default LogLevel it costs nothing.
			//
			// WRITES NOTHING.
			void ProbeSidebarMixEntry(unsigned long sideIndex)
			{
				static int reportsLeft = 8;
				if (reportsLeft <= 0)
					return;

				--reportsLeft;

				Karst::LogDebug(L"ShowByCountry[mixentry] 0x534FA0 entered: "
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
