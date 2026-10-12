#pragma once

#include <Windows.h>

#include <map>
#include <string>
#include <vector>

// IniHotReload, support for the reload itself - WHICH sections changed.
//
// A reload used to refeed every type of a whitelisted kind (all 402 building
// types), which measured at ~300 ms because each type's LoadFromINI re-reads a
// large part of the rules. Almost all of that work was wasted: the user edited
// one or two sections, not 402.
//
// This module answers the question the reload actually needs answered: "which
// sections of this file are different from the last time we read it?" The
// reload then refeeds only the type objects whose ID is one of those sections.
//
// WHY THE FILE IS PARSED HERE AND NOT VIA THE ENGINE
//
// The engine's CCINIClass could enumerate sections, but its exact API shape has
// to hold up inside the game process, and a mistake there is a crash. Parsing
// the text is a few milliseconds for a 750 KB rules file, is entirely under
// this module's control, and - importantly - can be reasoned about and tested
// without the game (see tools\check_snapshot.cpp).
//
// WHAT "CHANGED" MEANS HERE
//
//   * section missing from the new snapshot      -> added
//   * section present in both, same content       -> unchanged
//   * section present in both, different content  -> changed
//   * section present only in the old snapshot    -> removed (also refed: the
//     type has to see that its data is gone)
//
// A section that is UNCHANGED but LOST keys gets flagged as `forceRefeed`:
// that happens when one file of a multi-file setup is reread into a temporary
// object and the other files' keys are therefore absent. Refeeing such a
// section would overwrite live values with nothing, so the caller has to decide
// (it refeeds the type but logs it loudly).

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			// One section: its keys in file order, each with an exact-value
			// fingerprint (a literal "Key=Value" string) used for comparison.
			struct IniSection
			{
				std::map<std::string, std::string> Keys;
			};

			struct IniSnapshot
			{
				// Section names are stored lower case (INI is case-insensitive).
				std::map<std::string, IniSection> Sections;

				bool IsEmpty() const { return this->Sections.empty(); }
				void Clear() { this->Sections.clear(); }
			};

			// One section that came out different, and how.
			enum class SectionChange
			{
				Added,
				Changed,
				Removed,
				// Present in both with identical values, but the new read lost
				// keys it used to have - see the note above.
				LostKeys,
			};

			struct ChangedSection
			{
				std::string   Name;
				SectionChange Kind;
			};

			// Parses `text` (the whole file) into `out`.
			void BuildSnapshot(const std::string& text, IniSnapshot& out);

			// Appends every section that differs between `previous` and `current`
			// to `out`, in `current`'s order (removed sections last). Returns how
			// many were appended.
			size_t CollectChangedSections(const IniSnapshot& previous,
				const IniSnapshot& current,
				std::vector<ChangedSection>& out);

			const char* SectionChangeName(SectionChange kind);
		}
	}
}
