#pragma once

// "ShowByArt" - display a building using another building's ART section, so it
// looks like that building - and occupies that building's footprint too.
//
// Configuration lives in the building's own section, read from rulesmd.ini
// first and artmd.ini second:
//
//     [NAPOWR]
//     ShowByArt=GAPOWR            ; look like the Allied power plant
//
//     [NAMISL]
//     ShowByArt=GAWETH            ; ART section ...
//     ShowByArtType=GAWEAT        ; ... and its registered type, when they differ
//
// HOW IT WORKS
//
// The engine parses a building type's ART section exactly ONCE, from ImageFile.
// Renaming that section before the engine parses it makes the engine fill every
// key - Image, Buildup, ActiveAnim, IdleAnim, ProductionAnim, ... - from the
// target section, and then create and draw all of it with its own code, its own
// offsets and its own palette. Nothing is copied or drawn by this DLL.
//
// The FOOTPRINT is a game rule (rulesmd.ini `Foundation=`), not art, so it does
// not follow the section rename; it is copied separately from the imitated
// building's type as part of the same declaration.
//
// WHAT IS DELIBERATELY NOT HERE
//
// There is no per-owner ("show a different section depending on the owning
// country") code. It used to exist and was built on per-country type clones;
// a clone was `memcpy(source) + ImageFile=<target section>`, which paired the
// target section's art with the SOURCE type's animation names, and that is what
// made the activity animations wrong. It was removed, not merely disabled.
// See HANDOFF.md before reintroducing anything of that shape.

// The interface only passes pointers across, so the engine class never needs to
// be included here. The declaration must sit at global scope: inside the
// namespaces below it would declare a different type.
class BuildingTypeClass;

namespace Karst
{
	namespace Ext
	{
		namespace ShowByCountry
		{
			// Reads the ShowByArt declarations out of the INI.
			//
			// Idempotent, and called from RedirectSectionBeforeFullParse() - see
			// there for why that is the only entry point that can do it.
			void ParseSectionRedirects();

			// Called from the engine's full ART-section parser (0x45FE50) before it
			// reads ImageFile. Renaming the section here, and only here, lets the
			// engine parse the target section itself.
			//
			// `pType` is the BuildingTypeClass* from ECX.
			void RedirectSectionBeforeFullParse(void* pType);

			// Copies the imitated building's footprint onto every redirected type, so
			// the ground a building occupies follows its art.
			//
			// Safe to call repeatedly: it only writes when a value actually differs.
			// `when` is a short label used in the log.
			void ApplyFoundationFromSection(const char* when);

			// Returns true once the engine's building type list exists.
			//
			// Called from the main loop: the type list does not exist at the
			// executable entry point, so the caller retries until this succeeds.
			bool Init();
			bool IsInitialized();

			// Marks the feature active and applies the footprint copy once, as soon
			// as a match is running.
			void MarkGameRunning();

			// -------------------------------------------------------------------
			// Reports that the engine's sidebar-MIX function (0x534FA0) was entered,
			// and with which side index. Separates "the engine never took that path"
			// from "it did, but Ares' hook does not land where expected".
			// WRITES NOTHING.
			void ProbeSidebarMixEntry(unsigned long sideIndex);

			// -------------------------------------------------------------------
			// Per-COUNTRY sidebar MIX  (Sidebar.MixFileIndex)
			// -------------------------------------------------------------------
			//
			// Ares' `Sidebar.MixFileIndex` works per SIDE, because the engine only
			// passes the side index to the code that builds SIDECnnMD.MIX. This
			// reads the same key from the COUNTRY's section instead, so different
			// countries inside one side can use different sidebars:
			//
			//     [Americans]
			//     Sidebar.MixFileIndex=2      ; 1-based: 2 -> SIDEC02MD.MIX
			//
			//     [Russians]
			//     Sidebar.MixFileIndex=1
			//
			// The country comes from SessionClass::Instance->StartSpots[0]->Country
			// (the pick made in the game-setup screen). NOT from `[Basic] Player=`,
			// which in a skirmish is the player's NAME and resolves to no country.
			//
			// Applied in the hook at 0x5350D3, the point where the side index becomes
			// the file name. That address is not used by Ares, so the two coexist:
			// Ares decides by side, this overrides by country when a rule exists.
			//
			// Returns true and fills `mixIndex` only when the country declares the
			// key; a country without it is left completely untouched.
			bool CountrySidebarMixIndex(int& mixIndex);

			// -------------------------------------------------------------------
			// READ-ONLY probe: candidate sources for the player's country
			// -------------------------------------------------------------------
			//
			// Ares' `Sidebar.MixFileIndex` selects SIDECnnMD.MIX per SIDE. This
			// project wants it per COUNTRY, which means Karst has to know the
			// player's country at the moment the engine picks that MIX.
			//
			// The engine only ever passes the SIDE index into the function that
			// builds the name, so the probe reports every candidate source
			// (ScenarioClass::PlayerSideIndex, the Scenario fields that could be
			// HumanPlayerHouseTypeIndex, and HouseClass::Player's early pointers)
			// instead of assuming one.
			//
			// The two runs that used it settled the question: NO Scenario field
			// follows the country (0x2A00-0x2C00 was byte-identical for USA and
			// Russia), and HouseClass::Player held no country pointer either. The
			// country only exists transiently in EAX at 0x6877FB / 0x6878AD - see
			// CapturePlayerCountry above.
			//
			// WRITES NOTHING.
			void ProbeSidebarMixContext(unsigned long sideIndex);

			// Drops the cached list and per-match state. Safe between matches.
			void ResetForNewMatch();
		}
	}
}
