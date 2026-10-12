#pragma once

#include <Windows.h>

#include <string>

// IniHotReload, part 1 of 3 - WHICH files may be reloaded.
//
// The user writes a list in <game directory>\karst.hotreload.ini:
//
//     [IniHotReload]
//     Enabled=yes
//     Files=rulesmd.ini,artmd.ini,aimd.ini
//
// Everything outside that list is left completely alone - not watched, not
// reread, not touched. That list is the whole boundary of the feature.
//
// The '.ini' suffix is NOT configurable: it is hard-coded here, and an entry
// with any other extension is reported and dropped. The list is therefore, by
// construction, a list of INI files and nothing else.
//
// Validation rules (an entry that fails one is reported and ignored; the rest
// of the list still works):
//
//   * extension must be .ini (case-insensitive)
//   * the file must exist
//   * it must resolve inside the game directory (a path that escapes upwards
//     is refused, so the feature cannot be pointed at another install)
//
// This file deliberately touches neither the engine nor the file system beyond
// syntax/validation checks; it is plain logic and can be reasoned about (and
// tested) on its own.

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			// Name of the list file, looked up next to gamemd.exe.
			const char* TargetListFileName();

			// Maximum number of entries. The list is a fixed array because the
			// whole point of the feature is a short, curated list of rule INIs.
			const size_t MaxTargets = 16;

			// One accepted entry.
			struct Target
			{
				// What the user wrote, trimmed. This is what gets handed to
				// CCFileClass, which resolves against the current directory the
				// same way the engine does.
				char Name[260];

				// Fully qualified path in lower case, for event matching only.
				// Never given to the engine.
				std::wstring FullPath;
			};

			// Parses karst.hotreload.ini. Logs every rejection with its reason.
			//
			// Returns true when the feature should run: the file was found, the
			// section exists, Enabled is not "no", and at least one entry was
			// accepted. On failure the reason is in the log and Count() stays 0.
			//
			// `gameDirectory` must end with a backslash. By default the list file
			// is looked up inside that directory (which is what the game needs:
			// the game directory is where the DLL sits). `listFilePathOverride`
			// exists so a test can point at a config anywhere without having to
			// run from that directory.
			bool Load(const wchar_t* gameDirectory,
				const wchar_t* listFilePathOverride = nullptr);

			// Accepted entries, in the order the user wrote them. That order is
			// the order they will be reread in, which matters: reading an INI
			// merges into the class, so a later file overrides an earlier one.
			size_t Count();
			const Target& Get(size_t index);

			// True when `eventPath` (a full path from the watcher) is one of the
			// accepted entries. Case-insensitive; both sides are already fully
			// qualified when this is called.
			bool Matches(const std::wstring& eventPath);

			// --- which type kinds may be refed ----------------------------------
			//
			// A refeed runs that type's LoadFromINI, which is engine and extension
			// code that was only ever written to run during load. The safe set has
			// to be established by measurement, so the default stays at the kind
			// that has been verified (buildings) while the list is configurable:
			//
			//     Types=default      buildings only (the verified default)
			//     Types=all          every kind except TerrainType
			//     Types=none         nothing is refed (the file is still parsed)
			//     Types=buildings,weapons,warheads    an explicit list
			//
			// TerrainType is refused even when named: the reference
			// implementation measured a crash there.
			//
			// The numbers are AbstractType's own values. They are repeated here as
			// a local enum so this file does not have to include YRpp's headers -
			// which keeps the config logic testable outside the game (see
			// tools\check_targets.cpp). A static assertion in the .cpp pins them
			// to AbstractType, so a divergence cannot go unnoticed.
			enum class TypeKind : unsigned
			{
				BuildingType       = 7,
				UnitType           = 40,
				InfantryType       = 16,
				AircraftType       = 3,
				WeaponType         = 49,
				WarheadType        = 50,
				BulletType         = 9,
				AnimType           = 5,
				VoxelAnimType      = 42,
				ParticleType       = 23,
				ParticleSystemType = 25,
				OverlayType        = 21,
				SmudgeType         = 30,
				IsotileType        = 18,
				SuperWeaponType    = 32,
				HouseType          = 14,
				Side               = 28,
				// TerrainType (37) is deliberately absent: refeeding it was
				// measured as a crash by the reference implementation.
			};

			// Called by Load(); features ask ShouldReloadType() below.
			bool ShouldReloadType(unsigned abstractType);

			// A one-line, human-readable account of the configured type kinds,
			// for the startup log.
			const char* TypeKindSummary();

			// Single shared instance accessors; the module keeps one list.
			bool IsEnabled();
		}
	}
}
