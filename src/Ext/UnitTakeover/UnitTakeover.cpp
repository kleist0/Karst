#include <Ext/UnitTakeover/UnitTakeover.h>

#include <Karst.h>
#include <Core/Log.h>

#include <ObjectClass.h>
#include <TechnoClass.h>
#include <HouseClass.h>

#include <Windows.h>

namespace
{
	const char* const kSection = "UnitTakeover";
	const char* const kEnabledKey = "Enabled";

	bool g_enabled = false;
	bool g_wasDown = false;
	int g_lastReported = 0;

	bool IsKeyDown(int vk)
	{
		return (::GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	// Returns true only when the game window is in the foreground, so Alt+X
	// pressed in another application does not fire inside the game.
	bool IsGameForeground()
	{
		HWND foreground = ::GetForegroundWindow();
		if (!foreground)
			return false;

		HWND active = ::GetActiveWindow();
		return foreground == active;
	}

	void TransferSelection()
	{
		HouseClass* pPlayer = HouseClass::Player;
		DynamicVectorClass<TechnoClass*>* pArray = TechnoClass::Array;
		if (!pPlayer || !pArray)
			return;

		int transferred = 0;
		for (int i = 0; i < pArray->Count; ++i)
		{
			TechnoClass* pTechno = pArray->Items[i];
			if (!pTechno || !pTechno->IsSelected)
				continue;

			// announce=false: no "unit captured" voice line per unit.
			if (pTechno->SetOwningHouse(pPlayer, false))
				++transferred;
		}

		if (transferred > 0 && transferred != g_lastReported)
		{
			g_lastReported = transferred;
			Karst::LogInfo(L"UnitTakeover: %d selected unit(s) now belong to the local player.",
				transferred);
		}
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace UnitTakeover
		{
			void Init()
			{
				g_wasDown = false;
				g_lastReported = 0;

				const Karst::Config& config = KarstExtension::GetConfig();
				g_enabled = config.Reader().GetBool(kSection, kEnabledKey, false);

				Karst::LogInfo(L"UnitTakeover: %ls - select units and press Alt+X to take them.",
					g_enabled ? L"enabled" : L"disabled (write [UnitTakeover] Enabled=yes)");
			}

			void OnFrame()
			{
				if (!g_enabled)
					return;

				const bool down = IsGameForeground() && IsKeyDown(VK_MENU) && IsKeyDown('X');

				// Edge-triggered: only the frame the combination goes down.
				if (down && !g_wasDown)
					TransferSelection();

				g_wasDown = down;
			}
		}
	}
}
