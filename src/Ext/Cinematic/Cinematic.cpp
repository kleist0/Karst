#include <Ext/Cinematic/Cinematic.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Karst.h>

#include <windows.h>

// ---------------------------------------------------------------------------
// See Cinematic.h for the engine facts this depends on.
// ---------------------------------------------------------------------------

namespace Karst
{
	namespace Ext
	{
		namespace Cinematic
		{
			namespace
			{
				// ScenarioClass* (the global pointer, 0xA8B230). Read through
				// MemorySafety because YRpp binds it as a reference.
				constexpr unsigned ScenarioSlot = 0xA8B230;

				// ScenarioClass+0x35A2 - set by LockInput, cleared by UnlockInput.
				constexpr unsigned Scenario_Cinematic = 0x35A2;

				// The flag the input paths actually test.
				constexpr unsigned InputLocked = 0xA8ED9C;

				// WWMouseClass::Instance (0x887640) - only its vtable slot 4
				// (ShowCursor, 0x10) is used here.
				constexpr unsigned MouseSlot = 0x887640;

				const char* const kSection = "Cinematic";
				const char* const kKey = "KeepMouse";

				bool g_enabled = true;

				// Edge detection: the cursor only has to be re-shown once per lock,
				// not every frame.
				bool g_wasLocked = false;
				int g_reportsLeft = 8;

				bool ShowCursorNow()
				{
					__try
					{
						void* const mouse = *reinterpret_cast<void* const*>(MouseSlot);

						if (!mouse)
							return false;

						void** const vtable = *reinterpret_cast<void***>(mouse);

						if (!vtable || !vtable[4])
							return false;

						typedef void(__thiscall* Call0)(void*);
						reinterpret_cast<Call0>(vtable[4])(mouse);

						return true;
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return false;
					}
				}
			}

			void Init()
			{
				const Karst::Config& config = KarstExtension::GetConfig();

				if (!config.Enabled())
					return;

				g_enabled = config.Reader().GetBool(kSection, kKey, true);

				Karst::LogInfo(L"Cinematic: KeepMouse=%ls - %ls.",
					g_enabled ? L"yes" : L"no",
					g_enabled
						? L"a cinematic's LockInput will not disable the mouse"
						: L"the engine's own behaviour is left alone");
			}

			void OnFrame()
			{
				if (!g_enabled)
					return;

				void* const scenario = Karst::MemorySafety::SafeReadSlot<void*>(
					reinterpret_cast<void* const*>(ScenarioSlot));

				if (!scenario)
					return;

				const BYTE* const flag = reinterpret_cast<const BYTE*>(scenario)
					+ Scenario_Cinematic;

				if (!Karst::MemorySafety::IsReadable(flag, 1))
					return;

				if (*flag == 0)
				{
					g_wasLocked = false;
					return;
				}

				// The input handlers skip their work while this is set; the drawing
				// path does not read it, so clearing it costs nothing but the lock.
				BYTE* const locked = reinterpret_cast<BYTE*>(InputLocked);

				if (Karst::MemorySafety::IsReadable(locked, 1))
					*locked = 0;

				if (!g_wasLocked)
				{
					g_wasLocked = true;

					if (ShowCursorNow())
					{
						if (g_reportsLeft > 0)
						{
							--g_reportsLeft;

							Karst::LogInfo(L"Cinematic: input was locked for a "
								L"cinematic - the mouse is kept alive.");
						}
					}
					else if (g_reportsLeft > 0)
					{
						--g_reportsLeft;

						Karst::LogWarn(L"Cinematic: input was locked, but the mouse "
							L"object was not reachable - the pointer may stay hidden.");
					}
				}
			}
		}
	}
}
