#pragma once

// "UnitSell" - hold Alt+M and box-select: everything inside the dragged
// rectangle is sold and the money goes to the local player.
//
// WHY NOT IsSelected
//
// The engine only ever marks YOUR units as selected; enemy and neutral objects
// can be clicked (to attack) but never enter the selection. Selling based on
// IsSelected therefore only ever sold friendly units - measured. This module
// uses the cursor cell instead, so the drag rectangle covers any owner.
//
// HOW IT WORKS
//
// 1. The cursor cell is recorded every frame from DisplayClass::DecideAction
//    (0x692610, first argument) - the cell the engine itself resolved for the
//    cursor.
// 2. On Alt+M the cell at that moment becomes the rectangle's start.
// 3. While Alt+M is held, every frame walks TechnoClass::Array and sells every
//    unit whose cell falls inside the start..current rectangle.
// 4. Releasing Alt+M disarms it.
//
// Removal per kind:
//   * buildings: SetOwningHouse(local player) then ObjectClass::Sell(0), the
//     sidebar's own sale path (animation + removal).
//   * everything else: refund paid directly, then removed through the damage
//     path that CursorKill already proved safe.
//
// Refund is read from the rules INI's Cost= rather than TechnoTypeClass::Cost,
// because `[General] BuildCost` overwrites Cost on every type (this mod sets it
// to 1, which paid 0).
//
// CONFIGURATION (karst.ini)
//
//     [UnitSell]
//     Enabled=yes       ; required - anything else leaves it off

namespace Karst
{
	namespace Ext
	{
		namespace UnitSell
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the per-frame hook (KarstExtension::OnFrame).
			void OnFrame();

			// Called from the hook at 0x692610 with the cell the engine resolved
			// for the cursor (first argument of DecideAction).
			void OnCursorCell(const void* pCell);
		}
	}
}
