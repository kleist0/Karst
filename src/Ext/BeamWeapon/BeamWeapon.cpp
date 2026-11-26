#include <Ext/BeamWeapon/BeamWeapon.h>

#include <Karst.h>
#include <Core/Log.h>

#include <GameClasses.h>
#include <TechnoClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <CCINIClass.h>
#include <LaserDrawClass.h>
#include <Memory.h>

#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

namespace
{
	// Per-weapon configuration, read once from the weapon's rules section:
	//
	//   LBeam=yes
	//   LBeamDuration=10        frames the beam lives
	//   LBeamDamage=5           damage per frame to everything on the beam
	//   LBeamWidth=64           half-width of the damaging corridor (leptons)
	//   LBeamColor=255,0,0      beam colour (optional)
	//   LBeamIsHouseColor=no    use the owner's house colour
	struct BeamSpec
	{
		bool enabled = false;
		int duration = 10;
		int damage = 5;
		int width = 64;
		ColorStruct color = ColorStruct(255, 255, 255);
		bool houseColor = false;
	};

	// One live beam. Every frame everything within `width` of the from->to
	// segment takes `damage`, until `remaining` hits 0.
	struct ActiveBeam
	{
		CoordStruct from;
		CoordStruct to;
		TechnoClass* owner = nullptr;
		WarheadTypeClass* warhead = nullptr;
		int damage = 0;
		int width = 64;
		int remaining = 0;
	};

	bool g_enabled = false;
	std::unordered_map<WeaponTypeClass*, BeamSpec> g_specCache;
	std::vector<ActiveBeam> g_beams;

	// Shortest 2D distance from p to the segment a-b (the engine's world is
	// isometric; Z is not used for the corridor test).
	double DistanceToSegment(const CoordStruct& p, const CoordStruct& a, const CoordStruct& b)
	{
		const double abx = static_cast<double>(b.X - a.X);
		const double aby = static_cast<double>(b.Y - a.Y);
		const double apx = static_cast<double>(p.X - a.X);
		const double apy = static_cast<double>(p.Y - a.Y);

		const double len2 = abx * abx + aby * aby;
		double t = 0.0;
		if (len2 > 0.0)
			t = (apx * abx + apy * aby) / len2;

		if (t < 0.0) t = 0.0;
		if (t > 1.0) t = 1.0;

		const double dx = apx - t * abx;
		const double dy = apy - t * aby;
		return std::sqrt(dx * dx + dy * dy);
	}

	bool ReadBool(CCINIClass* pINI, const char* section, const char* key, bool fallback)
	{
		return pINI ? pINI->ReadBool(section, key, fallback) : fallback;
	}

	// Parses "r,g,b".
	ColorStruct ReadColor(CCINIClass* pINI, const char* section, const char* key, ColorStruct fallback)
	{
		if (!pINI)
			return fallback;

		char buffer[64] = {};
		if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
			return fallback;

		int r = 255, g = 255, b = 255;
		if (::sscanf_s(buffer, "%d,%d,%d", &r, &g, &b) < 1)
			return fallback;

		auto clamp = [](int v) { return static_cast<BYTE>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
		return ColorStruct(clamp(r), clamp(g), clamp(b));
	}

	// Raw-string reads: the INI layer mangles some values (comments etc.), and a
	// mangled number here used to silently become 1.
	int ReadRawInt(CCINIClass* pINI, const char* section, const char* key, int fallback)
	{
		if (!pINI)
			return fallback;

		char buffer[64] = {};
		if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0 || buffer[0] == '\0')
			return fallback;

		return ::atoi(buffer);
	}

	const BeamSpec* GetSpec(WeaponTypeClass* pWeapon)
	{
		if (!pWeapon)
			return nullptr;

		const auto found = g_specCache.find(pWeapon);
		if (found != g_specCache.end())
			return &found->second;

		BeamSpec spec;

		CCINIClass* pINI = CCINIClass::INI_Rules;
		const char* section = pWeapon->ID;
		if (pINI && section && section[0] != '\0')
		{
			const bool beam = ReadBool(pINI, section, "LBeam", false);
			if (beam)
			{
				spec.enabled = true;
				spec.duration = ReadRawInt(pINI, section, "LBeamDuration", 10);
				spec.damage = ReadRawInt(pINI, section, "LBeamDamage", 5);
				spec.width = ReadRawInt(pINI, section, "LBeamWidth", 64);
				spec.color = ReadColor(pINI, section, "LBeamColor", ColorStruct(255, 255, 255));
				spec.houseColor = ReadBool(pINI, section, "LBeamIsHouseColor", false);

				if (spec.duration < 1) spec.duration = 1;
				if (spec.damage < 0) spec.damage = 0;
				if (spec.width < 0) spec.width = 0;

				Karst::LogInfo(L"BeamWeapon: [%hs] enabled - duration %d, damage/frame %d, width %d.",
					section, spec.duration, spec.damage, spec.width);
			}
		}

		return &g_specCache.emplace(pWeapon, spec).first->second;
	}

	void ApplyBeam(void* pOwnerVoid, void* pWeaponVoid, void* pTargetVoid, int weaponIndex)
	{
		if (!g_enabled)
			return;

		TechnoClass* pOwner = static_cast<TechnoClass*>(pOwnerVoid);
		WeaponTypeClass* pWeapon = static_cast<WeaponTypeClass*>(pWeaponVoid);
		AbstractClass* pTarget = static_cast<AbstractClass*>(pTargetVoid);

		const BeamSpec* spec = GetSpec(pWeapon);
		if (!spec || !spec->enabled || !pOwner || !pTarget)
			return;

		// Muzzle -> target.
		CoordStruct from = pOwner->GetCoords();
		pOwner->GetFLH(&from, weaponIndex, from);
		const CoordStruct to = pTarget->GetCoords();

		ColorStruct inner = spec->color;
		ColorStruct outer = spec->color;
		if (spec->houseColor && pOwner->Owner)
		{
			// House colour hint; the engine may still render its own colour. The
			// configured colour stays the fallback.
			inner = spec->color;
			outer = spec->color;
		}

		GameCreate<LaserDrawClass>(from, to, inner, outer, ColorStruct(0, 0, 0), spec->duration);

		if (spec->damage <= 0 || !pWeapon->Warhead)
			return;

		ActiveBeam beam;
		beam.from = from;
		beam.to = to;
		beam.owner = pOwner;
		beam.warhead = pWeapon->Warhead;
		beam.damage = spec->damage;
		beam.width = spec->width;
		beam.remaining = spec->duration;

		g_beams.push_back(beam);
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace BeamWeapon
		{
			void Init()
			{
				g_specCache.clear();
				g_beams.clear();

				const Karst::Config& config = KarstExtension::GetConfig();
				g_enabled = config.Reader().GetBool("BeamWeapon", "Enabled", false);

				Karst::LogInfo(L"BeamWeapon: %ls - LBeam weapons fire a damaging beam.",
					g_enabled ? L"enabled" : L"disabled (write [BeamWeapon] Enabled=yes)");
			}

			void OnFire(void* pOwner, void* pWeapon, void* pTarget, int weaponIndex)
			{
				ApplyBeam(pOwner, pWeapon, pTarget, weaponIndex);
			}

			void OnFrame()
			{
				if (g_beams.empty() || !TechnoClass::Array)
					return;

				// Snapshot the unit list first: ReceiveDamage can kill a unit, and a
				// kill mutates TechnoClass::Array while we are iterating it (that
				// was the 0xC0000005).
				std::vector<TechnoClass*> victims;
				victims.reserve(TechnoClass::Array->Count);
				for (int k = 0; k < TechnoClass::Array->Count; ++k)
				{
					TechnoClass* pUnit = TechnoClass::Array->Items[k];
					if (pUnit && pUnit->IsAlive)
						victims.push_back(pUnit);
				}

				for (size_t i = 0; i < g_beams.size();)
				{
					ActiveBeam& beam = g_beams[i];

					if (beam.remaining <= 0)
					{
						g_beams.erase(g_beams.begin() + i);
						continue;
					}

					// The beam's owner may have died since it fired.
					if (!beam.owner || !beam.owner->IsAlive || !beam.warhead)
					{
						g_beams.erase(g_beams.begin() + i);
						continue;
					}

					HouseClass* pAttackerHouse = beam.owner->Owner;

					for (TechnoClass* pTechno : victims)
					{
						if (!pTechno || !pTechno->IsAlive)
							continue;

						const CoordStruct coords = pTechno->GetCoords();
						if (DistanceToSegment(coords, beam.from, beam.to) > static_cast<double>(beam.width))
							continue;

						int damage = beam.damage;
						pTechno->ReceiveDamage(&damage, 0, beam.warhead, beam.owner,
							false, false, pAttackerHouse);
					}

					--beam.remaining;
					++i;
				}
			}
		}
	}
}
