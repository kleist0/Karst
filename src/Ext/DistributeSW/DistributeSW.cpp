#include <Ext/DistributeSW/DistributeSW.h>
#include <Ext/EngineLayout.h>

#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <CCINIClass.h>
#include <Fundamentals.h>
#include <HouseClass.h>
#include <MessageListClass.h>
#include <SuperClass.h>
#include <SuperWeaponTypeClass.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>

#include <Windows.h>

#include <climits>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	// Our own frame counter, advanced once per OnFrame.
	//
	// The engine's Unsorted::CurrentFrame (0xA8ED84) was the first choice, but a
	// live run showed every charge stuck at "0 ready" with a 10-frame recharge -
	// which is exactly what a clock that does not advance looks like. Nothing here
	// needs engine time: a charge is "N frames from now", and OnFrame runs once
	// per frame, so counting our own ticks is sufficient and independent.
	int g_frame = 0;
	struct GroupKey
	{
		void* House;
		void* SWType;

		bool operator<(const GroupKey& other) const
		{
			if (House != other.House)
				return House < other.House;

			return SWType < other.SWType;
		}
	};

	// (house, superweapon type) -> (building -> frame its charge becomes ready)
	typedef std::map<GroupKey, std::map<void*, int>> GroupMap;
	GroupMap g_groups;

	// What this module last wrote into each SuperClass::IsCharged.
	std::map<GroupKey, bool> g_lastCharged;

	// DistributeBuild=yes per SuperWeaponTypeClass*, read once from the rules INI.
	std::unordered_map<void*, bool> g_distributeCache;

	// Diagnostics: log each type once, and complain once if the engine fires a
	// charge we do not think exists.
	std::unordered_map<void*, bool> g_loggedType;
	std::unordered_map<void*, bool> g_warnedNoSuper;
	int g_freeFireWarned = 0;

	bool Safe(int frameReadyAt)
	{
		return frameReadyAt > 0;
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace DistributeSW
		{
			namespace
			{
				bool IsReadable(const void* p)
				{
					return p && Karst::MemorySafety::IsReadable(p);
				}

				bool DistributeEnabled(SuperWeaponTypeClass* swType)
				{
					if (!IsReadable(swType))
						return false;

					auto cached = g_distributeCache.find(swType);

					if (cached != g_distributeCache.end())
						return cached->second;

					bool enabled = false;

					CCINIClass* ini = Karst::MemorySafety::SafeReadSlot<CCINIClass*>(&CCINIClass::INI_Rules);

					if (IsReadable(ini) && IsReadable(swType->ID))
						enabled = ini->ReadBool(swType->ID, "DistributeBuild", false);

					g_distributeCache[swType] = enabled;

					Karst::LogDebug(L"DistributeSW: '%hs' DistributeBuild=%d.", swType->ID, enabled ? 1 : 0);

					if (enabled)
					{
						Karst::LogInfo(L"DistributeSW: '%hs' has DistributeBuild=yes - "
							L"every building adds its own charge.", swType->ID);
					}

					return enabled;
				}

				// The engine keeps one SuperClass per type in HouseClass::Supers.
				// FindSuperWeapon(SuperWeaponType) matches the engine's enum, which
				// is shared by every type of that kind, so match the type object.
				SuperClass* FindSuper(HouseClass* house, SuperWeaponTypeClass* swType)
				{
					if (!IsReadable(house))
						return nullptr;

					for (int i = 0; i < house->Supers.Count; ++i)
					{
						SuperClass* super = house->Supers.Items[i];

						if (IsReadable(super) && super->Type == swType)
							return super;
					}

					return nullptr;
				}

				// BuildingClass::Type is at 0x520 in gamemd 1.001 (written by the
				// constructor at 0x43B756; recorded in Ext\EngineLayout.h). The YRpp
				// member is deliberately not used: reads through it produced an
				// unreadable pointer for every building in a live match, which is what
				// the in-game diagnostic reported before this change.
				BuildingTypeClass* BuildingTypeOf(BuildingClass* building)
				{
					const BYTE* base = reinterpret_cast<const BYTE*>(building);

					if (!Karst::MemorySafety::IsReadable(
						base + Karst::Ext::EngineLayout::Building_Type, sizeof(void*)))
					{
						return nullptr;
					}

					BuildingTypeClass* type = *reinterpret_cast<BuildingTypeClass* const*>(
						base + Karst::Ext::EngineLayout::Building_Type);

					return IsReadable(type) ? type : nullptr;
				}

				bool IsOneOfTheHouses(const void* candidate)
				{
					DynamicVectorClass<HouseClass*>* houses =
						Karst::MemorySafety::SafeReadSlot<DynamicVectorClass<HouseClass*>*>(&HouseClass::Array);

					if (!houses || !Karst::MemorySafety::IsReadable(houses))
						return false;

					for (int i = 0; i < houses->Count; ++i)
					{
						if (houses->Items[i] == candidate)
							return true;
					}

					return false;
				}

				// The house a building belongs to.
				//
				// TechnoClass::Owner (0x108) is documented in EngineLayout.h as NOT
				// trustworthy - a live construction yard read 0 or 1 from it. So the
				// pointer is only accepted when it really is one of the engine's
				// HouseClass objects, and otherwise the object is searched for one.
				// A building is small, the house array is short, and the scan runs at
				// most a few times per building, so the cost stays negligible.
				HouseClass* OwnerOf(BuildingClass* building)
				{
					static std::unordered_map<void*, HouseClass*> found;
					static std::unordered_map<void*, int> attempts;

					auto cached = found.find(building);

					if (cached != found.end())
						return cached->second;

					if (attempts[building] >= 3)
						return nullptr;

					++attempts[building];

					HouseClass* owner = nullptr;
					const BYTE* base = reinterpret_cast<const BYTE*>(building);

					// +0x14C is where the search below has always landed on this
					// engine build (EngineLayout::Building_Owner); try it first and
					// keep the search as the fallback.
					void* measured = *reinterpret_cast<void* const*>(
						base + Karst::Ext::EngineLayout::Building_Owner);

					void* documented = *reinterpret_cast<void* const*>(
						base + Karst::Ext::EngineLayout::Techno_Owner);

					if (IsOneOfTheHouses(measured))
					{
						owner = static_cast<HouseClass*>(measured);
					}
					else if (IsOneOfTheHouses(documented))
					{
						owner = static_cast<HouseClass*>(documented);
					}
					else if (Karst::MemorySafety::IsReadable(base, 0x600))
					{
						for (unsigned offset = 0; offset < 0x600; offset += 4)
						{
							void* candidate = *reinterpret_cast<void* const*>(base + offset);

							if (candidate && IsOneOfTheHouses(candidate))
							{
								owner = static_cast<HouseClass*>(candidate);
								Karst::LogDebug(L"DistributeSW: owner resolved at +0x%X.",
									offset);
								break;
							}
						}
					}

					if (owner)
						found[building] = owner;

					return owner;
				}

				SuperWeaponTypeClass* SWTypeAt(int index)
				{
					DynamicVectorClass<SuperWeaponTypeClass*>* types = SuperWeaponTypeClass::Array;
					if (!types || !Karst::MemorySafety::IsReadable(types))
						return nullptr;

					if (index < 0 || index >= types->Count)
						return nullptr;

					SuperWeaponTypeClass* swType = types->Items[index];

					return IsReadable(swType) ? swType : nullptr;
				}

				bool SameType(const std::vector<SuperWeaponTypeClass*>& list, SuperWeaponTypeClass* swType)
				{
					for (SuperWeaponTypeClass* item : list)
					{
						if (item == swType)
							return true;
					}

					return false;
				}

				void AddSuperWeapon(std::vector<SuperWeaponTypeClass*>& list, const char* name)
				{
					if (!name)
						return;

					while (*name == ' ' || *name == '\t')
						++name;

					char trimmed[128] = { 0 };
					size_t length = 0;

					while (name[length] && name[length] != ' ' && name[length] != '\t'
						&& length + 1 < sizeof(trimmed))
					{
						trimmed[length] = name[length];
						++length;
					}

					if (length == 0)
						return;

					DynamicVectorClass<SuperWeaponTypeClass*>* types = SuperWeaponTypeClass::Array;

					if (!types || !Karst::MemorySafety::IsReadable(types))
						return;

					SuperWeaponTypeClass* swType = SuperWeaponTypeClass::Find(trimmed);

					if (IsReadable(swType) && !SameType(list, swType))
					{
						list.push_back(swType);
						Karst::LogDebug(L"DistributeSW:   + %hs", trimmed);
					}
				}
				// The superweapons a building type provides.
				//
				// Vanilla has the two scalar keys. Mods that add more than two (the
				// `SuperWeapons=` list, as Ares/Phobos-style packages use it) name
				// them by superweapon section ID, so they are resolved through
				// SuperWeaponTypeClass::Find. Cached per building type: the list
				// cannot change without a rules reload, which reinstantiates types.
				const std::vector<SuperWeaponTypeClass*>& SuperWeaponsOf(BuildingTypeClass* type)
				{
					static std::unordered_map<void*, std::vector<SuperWeaponTypeClass*>> cache;

					auto cached = cache.find(type);

					if (cached != cache.end())
						return cached->second;

					std::vector<SuperWeaponTypeClass*> list;

					// Names, not struct offsets.
					//
					// The scalar fields in BuildingTypeClass do not line up with this
					// engine build: reading them made a barracks "provide a superweapon"
					// while the Iron Curtain building resolved to a different type. The
					// rules INI is authoritative and names superweapon sections, so the
					// keys are read as strings and resolved by name.
					CCINIClass* ini = Karst::MemorySafety::SafeReadSlot<CCINIClass*>(&CCINIClass::INI_Rules);

					if (IsReadable(ini) && IsReadable(type->ID))
					{
						char buffer[1024] = { 0 };

						const char* const scalars[2] = { "SuperWeapon", "SuperWeapon2" };

						for (const char* key : scalars)
						{
							buffer[0] = '\0';
							ini->ReadString(type->ID, key, "", buffer, sizeof(buffer));
							AddSuperWeapon(list, buffer);
						}

						buffer[0] = '\0';
						ini->ReadString(type->ID, "SuperWeapons", "", buffer, sizeof(buffer));

						char* cursor = buffer;

						while (cursor && *cursor)
						{
							char* comma = cursor;

							while (*comma && *comma != ',')
								++comma;

							const char saved = *comma;
							*comma = '\0';

							AddSuperWeapon(list, cursor);

							if (saved == '\0')
								break;

							cursor = comma + 1;
						}
					}
					std::vector<SuperWeaponTypeClass*>& stored = cache[type];
					stored.swap(list);

					Karst::LogDebug(L"DistributeSW: '%hs' provides %d superweapon(s).",
						IsReadable(type->ID) ? type->ID : "?", static_cast<int>(stored.size()));

					return stored;
				}

				int RechargeTimeOf(SuperClass* super, SuperWeaponTypeClass* swType)
				{
					int time = 0;

					if (IsReadable(super))
						time = super->GetRechargeTime();
					else if (IsReadable(swType))
						time = swType->RechargeTime;

					return time > 0 ? time : 1;
				}

				void NoteTypeSeen(SuperWeaponTypeClass* swType, int buildings)
				{
					if (!IsReadable(swType))
						return;

					if (g_loggedType[swType])
						return;

					g_loggedType[swType] = true;
					Karst::LogInfo(L"DistributeSW: '%hs' is in use - %d building(s) provide it.",
						swType->ID, buildings);
				}
			}

			// Shows "<id>  ready / total" in the game's message list whenever the
			// number of available charges changes: +1 when a building's charge
			// finishes, -1 when one is spent. Only the local player's own house is
			// announced, and only while the engine's message list exists - both are
			// engine globals, so both go through MemorySafety.
			void Announce(HouseClass* house, SuperWeaponTypeClass* swType, int ready, int total)
			{
				if (!IsReadable(swType) || !IsReadable(house))
					return;

				HouseClass* player =
					Karst::MemorySafety::SafeReadSlot<HouseClass*>(&HouseClass::Player);

				if (!IsReadable(player) || player != house)
					return;

				MessageListClass* messages = MessageListClass::Instance;

				if (!IsReadable(messages))
					return;

				wchar_t text[160] = { 0 };

				_snwprintf_s(text, _TRUNCATE, L"%hs  %d / %d",
					swType->ID, ready, total);

				messages->PrintMessage(text, 0x96, ColorScheme::Yellow, false);
			}

			void Init()
			{
				Karst::LogInfo(L"DistributeSW: watching DistributeBuild=yes in superweapon type sections.");
			}

			void OnFrame()
			{
				DynamicVectorClass<BuildingClass*>* buildings =
					Karst::MemorySafety::SafeReadSlot<DynamicVectorClass<BuildingClass*>*>(&BuildingClass::Array);

				if (!buildings || !Karst::MemorySafety::IsReadable(buildings) || buildings->Count <= 0)
					return;

				const int now = ++g_frame;

				static bool s_firstScanLogged = false;

				int scanned = 0;
				int unreadableTypes = 0;
				int withSuperWeapon = 0;

				GroupMap next;

				for (int i = 0; i < buildings->Count; ++i)
				{
					BuildingClass* building = buildings->Items[i];

					if (!IsReadable(building) || !building->IsAlive || building->InLimbo)
						continue;

					++scanned;

					BuildingTypeClass* type = BuildingTypeOf(building);

					if (!IsReadable(type))
					{
						++unreadableTypes;
						continue;
					}

					HouseClass* house = OwnerOf(building);

					if (!IsReadable(house))
					{
						++unreadableTypes;
						continue;
					}

					const std::vector<SuperWeaponTypeClass*>& swList = SuperWeaponsOf(type);

					if (!swList.empty())
						++withSuperWeapon;

					for (SuperWeaponTypeClass* swType : swList)
					{
						if (!DistributeEnabled(swType))
							continue;

						GroupKey key = { house, swType };
						std::map<void*, int>& slots = next[key];

						if (slots.find(building) != slots.end())
							continue;

							NoteTypeSeen(swType, static_cast<int>(slots.size()) + 1);

							int readyAt = 0;

							// Carry an existing charge over; a building seen for the
							// first time starts its own recharge now.
							auto group = g_groups.find(key);

							if (group != g_groups.end())
							{
								auto slot = group->second.find(building);

								if (slot != group->second.end())
									readyAt = slot->second;
							}

							if (!Safe(readyAt))
							{
								SuperClass* super = FindSuper(house, swType);
								const int recharge = RechargeTimeOf(super, swType);
								readyAt = now + recharge;
								
								Karst::LogDebug(L"DistributeSW: '%hs' charge starts - now=%d recharge=%d.",
										swType->ID, now, recharge);
							}

							slots[building] = readyAt;
						}
					}

				if (!s_firstScanLogged && scanned > 0)
				{
					s_firstScanLogged = true;
					Karst::LogInfo(L"DistributeSW: first scan - %d building(s), %d with an unreadable "
						L"type or owner, %d name a superweapon.",
						scanned, unreadableTypes, withSuperWeapon);
				}

				g_groups.swap(next);

				// Drive the engine's single cameo from our per-building timers.
				//
				// Consumption is detected from the engine's own state, not from a
				// hook. Ares and Phobos both own the head of SuperClass::Launch AND
				// of SuperClass::ClickFire; hooks placed past theirs never ran in a
				// live game (measured: charges reached "ready", every launch produced
				// no callback). What cannot be avoided is the engine clearing
				// IsCharged when a charge is actually spent, so that transition is
				// the signal: this loop sets it true every frame while a charge is
				// ready, so a false value on the next frame means a charge was spent.
				for (auto& group : g_groups)
				{
					HouseClass* house = static_cast<HouseClass*>(group.first.House);
					SuperWeaponTypeClass* swType = static_cast<SuperWeaponTypeClass*>(group.first.SWType);

					if (!swType)
						continue;

					SuperClass* super = FindSuper(house, swType);

					if (!super)
					{
						// The house owns buildings that carry this type, but the
						// engine has not created its SuperClass yet. Say it once so
						// "nothing happened" is diagnosable.
						if (!g_warnedNoSuper[swType])
						{
							g_warnedNoSuper[swType] = true;
							Karst::LogWarn(L"DistributeSW: '%hs' has %d building(s) but the house "
								L"owns no SuperClass for it yet - waiting.",
								swType->ID, static_cast<int>(group.second.size()));
						}

						continue;
					}

					const bool engineCharged = super->IsCharged;

					auto last = g_lastCharged.find(group.first);

					if (last != g_lastCharged.end() && last->second && !engineCharged)
					{
						void* spent = nullptr;
						int spentReadyAt = INT_MAX;

						for (const auto& slot : group.second)
						{
							if (slot.second <= now && slot.second < spentReadyAt)
							{
								spent = slot.first;
								spentReadyAt = slot.second;
							}
						}

						if (spent)
						{
							group.second[spent] = now + RechargeTimeOf(super, swType);
							Karst::LogDebug(L"DistributeSW: '%hs' spent one charge - "
								L"%d building(s) tracked.",
								swType->ID, static_cast<int>(group.second.size()));
						}
						else
						{
							Karst::LogDebug(L"DistributeSW: '%hs' engine cleared IsCharged but no "
								L"charge was ready.", swType->ID);
						}
					}

					int ready = 0;
					int minimumRemaining = INT_MAX;

					for (const auto& slot : group.second)
					{
						const int remaining = slot.second - now;

						if (remaining <= 0)
							++ready;
						else if (remaining < minimumRemaining)
							minimumRemaining = remaining;
					}

					super->IsCharged = ready > 0;
					g_lastCharged[group.first] = super->IsCharged;

					// The cameo progress belongs to whichever charge finishes
					// first, so a partially charged batch still shows movement.
					super->RechargeTimer.StartTime = now;
					super->RechargeTimer.TimeLeft =
						ready > 0 ? 0 : (minimumRemaining == INT_MAX ? 0 : minimumRemaining);

					// One line per real change, so "how many charges are tracked and
					// how many are ready" is readable from the log instead of guessed.
					static std::map<void*, std::pair<int, int>> lastState;
					const std::pair<int, int> state =
						{ static_cast<int>(group.second.size()), ready };

					auto previous = lastState.find(swType);
					const bool readyChanged =
						previous != lastState.end() && previous->second.second != state.second;

					if (previous == lastState.end() || previous->second != state)
					{
						lastState[swType] = state;
						Karst::LogDebug(L"DistributeSW: '%hs' - %d building(s), %d charge(s) ready.",
							swType->ID, state.first, state.second);

						if (readyChanged)
							Announce(house, swType, state.second, state.first);
					}
				}
			}
		}
	}
}
