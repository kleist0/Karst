#include <Ext/EmptyReloadSound/EmptyReloadSound.h>

#include <Karst.h>
#include <Core/Log.h>

#include <GameClasses.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <WeaponTypeClass.h>
#include <CCINIClass.h>
#include <VocClass.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>
#include <Memory.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
	const char* const kSection = "EmptyReloadSound";
	const char* const kEnabledKey = "Enabled";
	const char* const kSoundKey = "EmptyReloadSound";	// key inside a weapon section

	// The engine calls the reload routine repeatedly while a unit is reloading,
	// so the same unit is suppressed for this many frames after one sound.
	const int kCooldownFrames = 60;

	// 0xA8ED84 is the frame counter the engine itself reads in the same routine.
	const DWORD kFrameCounter = 0xA8ED84;

	bool g_enabled = false;

	// WeaponType -> sound index (-1 = the weapon declares no sound, cached so the
	// INI is not queried per reload).
	std::unordered_map<TechnoTypeClass*, int> g_soundCache;
	std::unordered_map<TechnoTypeClass*, AnimTypeClass*> g_animCache;

	std::unordered_map<TechnoClass*, int> g_lastPlayedFrame;

	int CurrentFrame()
	{
		return *reinterpret_cast<int*>(kFrameCounter);
	}

	// Plays a Voc index at a position, calling the engine entry point directly.
	//
	// NOT VocClass::PlayAt: YRpp declares it __fastcall but forwards with JMP_STD,
	// so ECX (the sound index) and EDX (the coordinate pointer) never arrive and
	// nothing is played. 0x7509E0 is a fastcall with a single stack argument:
	//
	//   ECX = index, EDX = &coords, [esp+4] = AudioController* (may be null),
	//   the function itself does et 4.
	void PlayVocAt(int index, const CoordStruct& coords)
	{
		using PlayAtFn = void(__fastcall*)(int, const CoordStruct*, void*);
		static PlayAtFn const fn = reinterpret_cast<PlayAtFn>(0x7509E0);
		fn(index, &coords, nullptr);
	}

	// Finds a Voc index by name by walking the engine's sound array directly.
	//
	// NOT VocClass::FindIndex: YRpp declares it __fastcall but forwards with
	// JMP_STD, so the name pointer never reaches ECX. Measured: it returned 255
	// for a name that - via this walk - resolves correctly.
	int FindVocIndex(const char* name)
	{
		if (!name)
			return -1;

		// Call the engine's own lookup rather than re-implementing it.
		//
		// 0x7514D0 is __fastcall(const char* /*ECX*\/) -> int, returning -1 when
		// not found. YRpp's FindIndex forwards with JMP_STD, which is why its
		// name never arrived and it answered 255; reading the array blindly also
		// failed (Count came back 0 through YRpp's binding).
		using FindIndexFn = int(__fastcall*)(const char*);
		static FindIndexFn const fn = reinterpret_cast<FindIndexFn>(0x7514D0);
		return fn(name);
	}

	// The EmptyReloadAnim= animation for a unit type, or null.
	AnimTypeClass* GetTypeAnim(TechnoTypeClass* pType)
	{
		if (!pType)
			return nullptr;

		const auto found = g_animCache.find(pType);
		if (found != g_animCache.end())
			return found->second;

		AnimTypeClass* pAnim = nullptr;

		CCINIClass* pINI = CCINIClass::INI_Rules;
		if (pINI && pType->ID[0] != '\0')
		{
			char buffer[64] = {};
			if (pINI->ReadString(pType->ID, "EmptyReloadAnim", "", buffer, sizeof(buffer)) > 0)
			{
				pAnim = AnimTypeClass::Find(buffer);
				if (!pAnim)
				{
					Karst::LogWarn(L"EmptyReloadSound: [%hs] EmptyReloadAnim='%hs' - animation not found.",
						pType->ID, buffer);
				}
			}
		}

		g_animCache[pType] = pAnim;
		return pAnim;
	}

	// Keyed by TechnoType, not WeaponType.
	//
	// The weapon was tried first and does not work on this build: YRpp's
	// TechnoTypeClass::Weapon array does not line up with the engine's layout
	// here, so GetWeapon(0/1, false) returned null for a unit (E2) that clearly
	// has a weapon (measured: weapon=?). The unit type is the reliable source
	// - GetTechnoType() and its ID are already proven by the rest of Karst.
	int GetTypeSound(TechnoTypeClass* pType)
	{
		if (!pType)
			return -1;

		const auto found = g_soundCache.find(pType);
		if (found != g_soundCache.end())
			return found->second;

		int index = -1;

		CCINIClass* pINI = CCINIClass::INI_Rules;
		if (pINI && pType->ID[0] != '\0')
		{
			char buffer[64] = {};
			if (pINI->ReadString(pType->ID, kSoundKey, "", buffer, sizeof(buffer)) > 0)
			{
				index = FindVocIndex(buffer);
				if (index < 0)
				{
					Karst::LogWarn(L"EmptyReloadSound: [%hs] %hs='%hs' - sound not found.",
						pType->ID, kSoundKey, buffer);
				}
			}
		}

		g_soundCache[pType] = index;
		return index;
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace EmptyReloadSound
		{
			void Init()
			{
				g_soundCache.clear();
				g_lastPlayedFrame.clear();

				const Karst::Config& config = KarstExtension::GetConfig();
				g_enabled = config.Reader().GetBool(kSection, kEnabledKey, false);

				if (!g_enabled)
				{
					Karst::LogInfo(L"EmptyReloadSound: disabled (write [EmptyReloadSound] Enabled=yes).");
					return;
				}

				Karst::LogInfo(L"EmptyReloadSound: enabled - a weapon's 'EmptyReloadSound=' plays when its unit starts an empty reload.");

			}

			void OnEmptyReload(void* pTechnoVoid)
			{
				if (!g_enabled)
					return;

				TechnoClass* pTechno = static_cast<TechnoClass*>(pTechnoVoid);
				if (!pTechno)
					return;

				TechnoTypeClass* pType = pTechno->GetTechnoType();
				const int index = GetTypeSound(pType);


				if (index < 0)
					return;

				const int frame = CurrentFrame();

				const auto found = g_lastPlayedFrame.find(pTechno);
				if (found != g_lastPlayedFrame.end() && frame - found->second < kCooldownFrames)
					return;

				g_lastPlayedFrame[pTechno] = frame;

				// Optional per-unit visual: [UnitType] EmptyReloadAnim=AnimName
				if (AnimTypeClass* pAnim = GetTypeAnim(pType))
					GameCreate<AnimClass>(pAnim, pTechno->GetCoords());

				PlayVocAt(index, pTechno->GetCoords());
			}
		}
	}
}
