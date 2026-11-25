#include <Ext/UnitSell/UnitSell.h>

#include <Karst.h>
#include <Core/Log.h>

#include <ObjectClass.h>
#include <TechnoClass.h>
#include <HouseClass.h>
#include <RulesClass.h>
#include <WarheadTypeClass.h>
#include <TechnoTypeClass.h>
#include <CCINIClass.h>
#include <CellClass.h>
#include <DisplayClass.h>

#include <Windows.h>
#include <algorithm>
#include <vector>

namespace
{
	const char* const kSection = "UnitSell";
	const char* const kEnabledKey = "Enabled";

	bool g_enabled = false;

	// Cursor cell, fed by the DecideAction hook.
	CellStruct g_cursorCell = { 0, 0 };
	bool g_hasCell = false;

	// Alt+M drag rectangle: the cell where the key went down, and whether the
	// drag is armed.
	bool g_armed = false;
	CellStruct g_startCell = { 0, 0 };

	int g_lastReported = 0;
	int g_lastAmount = 0;

	bool IsKeyDown(int vk)
	{
		return (::GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	bool IsGameForeground()
	{
		HWND foreground = ::GetForegroundWindow();
		if (!foreground)
			return false;

		return foreground == ::GetActiveWindow();
	}

	// Refund for one unit, in credits.
	//
	// Read from the rules INI rather than from TechnoTypeClass::Cost, because
	// `[General] BuildCost` (this mod sets it to 1) overwrites Cost on every type
	// at load time - the field paid 0 for everything.
	int ComputeRefund(TechnoClass* pTechno)
	{
		int amount = 0;
		if (TechnoTypeClass* pType = pTechno->GetTechnoType())
		{
			CCINIClass* pINI = CCINIClass::INI_Rules;
			if (pINI && pType->ID[0] != '\0')
				amount = pINI->ReadInteger(pType->ID, "Cost", 0) / 2;
		}

		if (amount <= 0)
			amount = 100;

		return amount;
	}

	void SellUnit(TechnoClass* pTechno, HouseClass* pPlayer, WarheadTypeClass* pWarhead)
	{
		// Everything, buildings included, is removed through the damage path
		// that CursorKill already proved safe.
		//
		// Why not the sale path for buildings: measured, SetOwningHouse + Sell(0)
		// only flips the owner and marks the building as selling (the object
		// stays on the map and turns friendly); ObjectClass::Sell locks the game
		// up on non-buildings. The refund is paid separately by ComputeRefund.
		(void)pPlayer;

		int damage = 1000000;
		pTechno->ReceiveDamage(&damage, 0, pWarhead, nullptr,
			false, false, nullptr);
	}

	// The cell under the cursor, taken every frame from DisplayClass.
	//
	// The DecideAction cell is not enough: that call only arrives when there is
	// an object under the cursor, which made the feature sell "whatever cell the
	// mouse points at" instead of a dragged rectangle. CurrentFoundation_CenterCell
	// is the engine's per-frame cursor cell.
	CellStruct GetCursorCell()
	{
		if (MapClass* pMap = MapClass::Instance)
		{
			DisplayClass* pDisplay = reinterpret_cast<DisplayClass*>(pMap);
			return pDisplay->CurrentFoundation_CenterCell;
		}

		return g_cursorCell;
	}

	void SellInRect(const CellStruct& a, const CellStruct& b)
	{
		HouseClass* pPlayer = HouseClass::Player;
		DynamicVectorClass<TechnoClass*>* pArray = TechnoClass::Array;
		if (!pPlayer || !pArray)
			return;

		RulesClass* pRules = RulesClass::Instance;
		WarheadTypeClass* pWarhead = pRules ? pRules->C4Warhead : nullptr;
		if (!pWarhead)
			return;

		const short minX = std::min(a.X, b.X);
		const short maxX = std::max(a.X, b.X);
		const short minY = std::min(a.Y, b.Y);
		const short maxY = std::max(a.Y, b.Y);

		// Snapshot first: removing objects mutates the array.
		std::vector<TechnoClass*> targets;
		for (int i = 0; i < pArray->Count; ++i)
		{
			TechnoClass* pTechno = pArray->Items[i];
			if (!pTechno)
				continue;

			const CellStruct cell = CellClass::Coord2Cell(pTechno->GetCoords());
			if (cell.X >= minX && cell.X <= maxX && cell.Y >= minY && cell.Y <= maxY)
				targets.push_back(pTechno);
		}

		int sold = 0;
		int total = 0;
		for (TechnoClass* pTechno : targets)
		{
			if (!pTechno || !pTechno->IsAlive)
				continue;

			const int amount = ComputeRefund(pTechno);
			if (amount > 0)
			{
				pPlayer->GiveMoney(amount);
				total += amount;
			}

			SellUnit(pTechno, pPlayer, pWarhead);
			++sold;
		}

		if (sold > 0 && (sold != g_lastReported || total != g_lastAmount))
		{
			g_lastReported = sold;
			g_lastAmount = total;
			Karst::LogInfo(L"UnitSell: %d unit(s) sold, +%d credits.", sold, total);
		}
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace UnitSell
		{
			void Init()
			{
				g_armed = false;
				g_hasCell = false;
				g_lastReported = 0;
				g_lastAmount = 0;

				const Karst::Config& config = KarstExtension::GetConfig();
				g_enabled = config.Reader().GetBool(kSection, kEnabledKey, false);

				Karst::LogInfo(L"UnitSell: %ls - hold Alt+M and box-select to sell.",
					g_enabled ? L"enabled" : L"disabled (write [UnitSell] Enabled=yes)");
			}

			void OnCursorCell(const void* pCell)
			{
				if (!pCell)
					return;

				g_cursorCell = *static_cast<const CellStruct*>(pCell);
				g_hasCell = true;
			}

			void OnFrame()
			{
				if (!g_enabled)
					return;

				const bool down = IsGameForeground() && IsKeyDown(VK_MENU) && IsKeyDown('M');

				if (!down)
				{
					g_armed = false;
					return;
				}

				const CellStruct current = GetCursorCell();

				if (!g_armed)
				{
					g_armed = true;
					g_startCell = current;
				}

				SellInRect(g_startCell, current);
			}
		}
	}
}
