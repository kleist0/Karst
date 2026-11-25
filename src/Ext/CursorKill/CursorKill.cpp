#include <Ext/CursorKill/CursorKill.h>

#include <Karst.h>
#include <Core/Log.h>

#include <AbstractClass.h>
#include <ObjectClass.h>
#include <HouseClass.h>
#include <RulesClass.h>
#include <WarheadTypeClass.h>

#include <algorithm>
#include <vector>

namespace
{
	const char* const kSection = "CursorKill";
	const char* const kEnabledKey = "Enabled";

	// OFF unless the user writes Enabled=yes. This is a destructive cheat-like
	// feature, so it must be opted into.
	bool g_enabled = false;

	// Objects recorded by the hook, resolved in the next OnFrame. Small: at most
	// the handful of objects the cursor touched since the last frame.
	std::vector<ObjectClass*> g_pending;

	bool IsKillableKind(AbstractType what)
	{
		return what == AbstractType::Infantry
			|| what == AbstractType::Unit
			|| what == AbstractType::Aircraft
			|| what == AbstractType::Building;
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace CursorKill
		{
			void Init()
			{
				g_pending.clear();

				const Karst::Config& config = KarstExtension::GetConfig();
				g_enabled = config.Reader().GetBool(kSection, kEnabledKey, false);

				Karst::LogInfo(L"CursorKill: %ls - hovering an enemy unit destroys it.",
					g_enabled ? L"enabled" : L"disabled (write [CursorKill] Enabled=yes)");
			}

			void OnHoverObject(void* pObject)
			{
				if (!g_enabled || !pObject)
					return;

				ObjectClass* pObj = static_cast<ObjectClass*>(pObject);

				if (!IsKillableKind(pObj->WhatAmI()))
					return;

				// The hook can fire several times for the same object per frame
				// (the engine re-queries the cursor while deciding the action).
				if (std::find(g_pending.begin(), g_pending.end(), pObj) == g_pending.end())
					g_pending.push_back(pObj);
			}

			void OnFrame()
			{
				if (g_pending.empty())
					return;

				if (!g_enabled)
				{
					g_pending.clear();
					return;
				}

				HouseClass* pLocal = HouseClass::Player;
				if (!pLocal)
				{
					g_pending.clear();
					return;
				}

				RulesClass* pRules = RulesClass::Instance;
				WarheadTypeClass* pWarhead = pRules ? pRules->C4Warhead : nullptr;
				if (!pWarhead)
				{
					g_pending.clear();
					return;
				}

				for (ObjectClass* pObject : g_pending)
				{
					if (!pObject || !pObject->IsAlive)
						continue;

					HouseClass* pOwner = pObject->GetOwningHouse();
					if (!pOwner || pLocal->IsAlliedWith(pOwner))
						continue;

					int damage = 1000000;
					pObject->ReceiveDamage(&damage, 0, pWarhead, nullptr,
						false, false, nullptr);
				}

				g_pending.clear();
			}
		}
	}
}
