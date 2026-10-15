#include <Ext/BuildCost/BuildCost.h>

#include <CCINIClass.h>
#include <Core/Log.h>
#include <Core/MemorySafety.h>

#include <Windows.h>
#include <climits>

namespace
{
	// Sentinel for "the user did not write the key". 0 is a legal cost (free),
	// so it cannot be used to mean "absent".
	const int kNotConfigured = INT_MIN;

	const char* const kGeneralSection = "General";
	const char* const kBuildCostKey = "BuildCost";

	// Diagnostics. The hook fires on the game thread while rules are loaded and
	// OnFrame runs on the same thread inside the main loop, so no locking is
	// needed.
	int g_configuredValue = kNotConfigured;	// value the last log line reported
	int g_appliedCount = 0;					// type sections priced so far
	int g_reportedCount = 0;				// how many of those are already logged
	bool g_firstSectionLogged = false;
	bool g_negativeWarned = false;
}

namespace Karst
{
	namespace Ext
	{
		namespace BuildCost
		{
			void Init()
			{
				// Nothing to prepare: the value can only be read from the rules INI
				// while the engine is reading that INI, which is what the hook at
				// 0x7146B3 does. This line exists so the log states that the feature
				// is installed - a user who sees no line here would otherwise have to
				// guess whether the DLL is even the one they built.
				Karst::LogInfo(L"BuildCost: watching [General] BuildCost in the rules INI.");
			}

			bool QueryOverride(void* pINI, const char* section, int& value)
			{
				CCINIClass* ini = reinterpret_cast<CCINIClass*>(pINI);

				// ESI at the hook is the CCINIClass the engine itself just called
				// ReadInteger on, so this checks a live object - but the load is
				// still guarded, because a wrong value here would call into a bogus
				// object on the game thread.
				if (!ini || !Karst::MemorySafety::IsReadable(ini))
					return false;

				const int configured = ini->ReadInteger(kGeneralSection, kBuildCostKey, kNotConfigured);

				if (configured == kNotConfigured)
					return false;

				if (configured < 0)
				{
					// A negative price would make the engine pay the player. Refuse it
					// rather than act on it, and say so once.
					if (!g_negativeWarned)
					{
						g_negativeWarned = true;
						Karst::LogWarn(L"BuildCost: [General] BuildCost=%d is negative - ignored.", configured);
					}
					return false;
				}

				value = configured;
				++g_appliedCount;

				if (configured != g_configuredValue)
				{
					g_configuredValue = configured;
					Karst::LogInfo(
						L"BuildCost: [General] BuildCost=%d - every TechnoType's Cost is now this value.",
						configured);
				}

				// One line naming a real section proves the hook is on the type
				// reader and not on something that merely looks like it.
				if (!g_firstSectionLogged && section && section[0] != '\0')
				{
					g_firstSectionLogged = true;
					Karst::LogInfo(L"BuildCost: first type priced from section '%hs'.", section);
				}

				return true;
			}

			void OnFrame()
			{
				if (g_appliedCount == g_reportedCount)
					return;

				g_reportedCount = g_appliedCount;
				Karst::LogInfo(L"BuildCost: %d type section(s) priced at %d.",
					g_appliedCount, g_configuredValue);
			}
		}
	}
}
