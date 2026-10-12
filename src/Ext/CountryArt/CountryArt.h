#pragma once

// CountryArt - "ShowByCountries" / "ShowBySHP": one building that looks different
// depending on the COUNTRY that owns it.
//
//     [GACNST]
//     ShowByCountries=Americans,Alliance
//     ShowBySHP=YACNST,NACNST
//
// Two parallel, index-aligned lists written in the building's own section: an
// American construction yard draws the YACNST art section, a Korean (Alliance)
// one draws NACNST, and a country that is not listed keeps the building's own
// art. The lists are validated against the engine's own country list; anything
// malformed is reported and ignored rather than half-applied.
//
// ---------------------------------------------------------------------------
// WHY A PER-COUNTRY TYPE COPY IS UNAVOIDABLE
// ---------------------------------------------------------------------------
//
// `ImageFile` lives on BuildingTypeClass, and every instance of a building
// shares ONE type object, so "same building, different look per owner" cannot be
// expressed by editing a type - it needs a copy of the type per country, with
// the building instance pointing at the copy its owner's country selects.
//
// ---------------------------------------------------------------------------
// WHY THE ENGINE, NOT THIS DLL, PARSES THE ART
// ---------------------------------------------------------------------------
//
// A copy built with memcpy alone carries the SOURCE type's already-parsed art
// fields (animation names, SHP pointers, ...), which is exactly the "body from
// one section, animations from another" failure this feature was withdrawn for
// once. Everything the engine fills in itself must therefore be filled in by the
// engine: the copy is made BEFORE the engine parses its source, is pointed at the
// target section, and is then run through the engine's own ART parser. This DLL
// decides which section, and nothing else - it never draws, never creates an
// object and never hand-copies an animation name.
//
// ---------------------------------------------------------------------------
// WHY THE COPIES ARE REBUILT FOR EVERY MATCH
// ---------------------------------------------------------------------------
//
// The engine recreates its building type objects when a new match starts: the
// same building has a different type address in the second match of one process
// (measured: GACNST 0x16A370D0 in match 1, 0x16E33040 in match 2). A copy kept
// from the previous match consequently holds pointers into a match that no
// longer exists, and the second match dies - measured with a crash dump, in
// Phobos' proximity check, on a stale inline extension slot.
//
// So the copies are tied to the source type they were made from: whenever the
// engine hands this module a type object that is not the one the copies belong
// to, the copies are discarded and remade from the CURRENT object. Nothing in
// this module survives a match boundary on purpose.
//
// ---------------------------------------------------------------------------
// THIS IS AN ADDITIVE MODULE
// ---------------------------------------------------------------------------
//
// It shares no state and no code with ShowByArt (Ext\ShowByCountry) and changes
// none of that module's behaviour. It is completely inert - no allocation, no
// engine call - until some building declares ShowByCountries. The two features
// are neighbours only in that both want to act before the engine reads a
// building type's ART, so dllmain calls both from the same hook, in that order.

class BuildingTypeClass;

namespace Karst
{
	namespace Ext
	{
		namespace CountryArt
		{
			// Called from the hook on the engine's complete ART parser (0x45FE50),
			// right after ShowByArt's section redirect.
			//
			// `pType` is the BuildingTypeClass* the engine passed in ECX; `ini` is
			// the CCINIClass* it passed as the first stack argument. Reads the rules
			// on its first useful call, makes (or remakes) this type's per-country
			// copies, and mirrors the parse onto them.
			void OnArtParse(void* pType, void* ini);

			// Called from the hook on BuildingClass's constructor (0x43B756), one
			// instruction before the instance's Type is stored.
			//
			// Returns the type the building should use: the copy belonging to the
			// owner's country, or `pType` unchanged. `pOwner` is the constructor's
			// second stack argument (a HouseClass*); the instance's own Owner field
			// is not written yet at this point, which is why the country has to come
			// from the argument.
			void* TypeForOwner(void* pType, void* pOwner);

			// Called from the hook on the engine's cameo lookup
			// (TechnoTypeClass::GetCameo, 0x712040), which the side bar goes through
			// to draw a building's icon.
			//
			// The side bar lists buildable types straight out of
			// BuildingTypeClass::Array - the SOURCE types - so a copy's own icon can
			// never reach it. This returns the copy that belongs to the LOCAL
			// PLAYER's country, so that the entry the player sees matches the
			// building they will get; it returns nullptr for any type this module
			// does not own, and when the local player's country is unknown.
			void* TypeForCameo(void* pType);

			// Called once per frame from the main loop. Does nothing until copies
			// exist; then, once, prints the source-versus-copy field comparison that
			// shows whether a copy really took the target section's art.
			void OnFrame();
		}
	}
}
