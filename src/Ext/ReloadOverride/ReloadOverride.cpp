#include <Ext/ReloadOverride/ReloadOverride.h>

#include <Karst.h>
#include <Core/Log.h>

#include <GameClasses.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>

#include <climits>

namespace
{
	// Written into the reload timer when the reload must never finish. The timer
	// clamps a remaining time <= 0 to 0, and INT_MAX stays positive for the whole
	// match, so the unit remains "reloading".
	const int kNeverSentinel = INT_MAX;

	// Offsets measured in the reload routine itself (gamemd 1.001):
	//   0x006FB094  mov ecx, [esi + 0x2FC]   ; current ammo
	//   0x006FB08E  mov eax, [eax + 0x684]   ; max ammo (from the type)
	//   0x006FB154  mov [esi + 0x1FC], ...   ; timer.StartTime  (after add esi,0x1FC)
	//   0x006FB15D  mov [esi + 0x1FC + 8]    ; timer.TimeLeft
	const size_t kAmmoOffset = 0x2FC;
	const size_t kMaxAmmoOffset = 0x684;
	const size_t kTimerTimeLeftOffset = 0x204;	// 0x1FC + 8

	bool g_enabled = false;
}

namespace Karst
{
	namespace Ext
	{
		namespace ReloadOverride
		{
			void Init()
			{
				const Karst::Config& config = KarstExtension::GetConfig();
				g_enabled = config.Reader().GetBool("ReloadOverride", "Enabled", false);

				Karst::LogInfo(L"ReloadOverride: %ls - Reload=-1 stops normal reload, EmptyReload=-1 stops empty reload.",
					g_enabled ? L"enabled" : L"disabled (write [ReloadOverride] Enabled=yes)");
			}

			void OnReloadReturn(void* pTechnoVoid)
			{
				if (!g_enabled)
					return;

				TechnoClass* pTechno = static_cast<TechnoClass*>(pTechnoVoid);
				if (!pTechno)
					return;

				TechnoTypeClass* pType = pTechno->GetTechnoType();
				if (!pType)
					return;

				unsigned char* pBytes = reinterpret_cast<unsigned char*>(pTechno);

				const int ammo = *reinterpret_cast<int*>(pBytes + kAmmoOffset);
				const int maxAmmo = *reinterpret_cast<int*>(
					reinterpret_cast<unsigned char*>(pType) + kMaxAmmoOffset);

				// The "nothing to reload" path also returns here; leave it alone.
				if (ammo >= maxAmmo)
					return;

				const int value = (ammo == 0) ? pType->EmptyReload : pType->Reload;
				if (value >= 0)
					return;

				*reinterpret_cast<int*>(pBytes + kTimerTimeLeftOffset) = kNeverSentinel;
			}
		}
	}
}
