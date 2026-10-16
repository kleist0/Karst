#include <Ext/VirtualSeat/VirtualSeat.h>

#include <Karst.h>
#include <Core/Log.h>
#include <Core/MemorySafety.h>

// GameClasses.h pulls the engine classes in dependency order; including a
// derived class header on its own leaves an unfinished base behind
// (the lesson recorded in Ext\CountryArt\CountryArt.cpp).
#include <GameClasses.h>
#include <CCINIClass.h>
#include <GenericList.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <FootClass.h>
#include <HouseClass.h>

// GameDelete: releases memory through the ENGINE's deallocator, which is the
// pairing rule in Memory.h for objects that came from Type->CreateObject().
#include <Memory.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// WHY THE HOOK IS ON A RULES READER AND NOT ON SOMETHING ELSE
//
// 0x7143BD is the `push "OpenTopped"` inside the engine's per-type rules
// reader. At that instruction the registers are:
//
//     EBP = the TechnoTypeClass being read
//     EBX = its section name (the type's ID)
//     ESI = the CCINIClass the engine is reading from
//
// which is everything this feature needs, and it already covers every type
// class (infantry, units, aircraft, buildings) because that reader is shared.
// Measured: none of Ares / Phobos / Kratos hooks this address or its
// neighbourhood, so nothing has to be chained.
//
// The key is read from that live INIClass rather than from the
// CCINIClass::INI_Rules singleton on purpose: a hot reload builds a temporary
// INIClass, and the singleton would keep answering with the startup copy.
// Same reasoning as Ext\BuildCost.
// ---------------------------------------------------------------------------

namespace
{
	const char* const kSection = "VirtualSeat";
	const char* const kEnabledKey = "Enabled";
	const char* const kDryRunKey = "DryRun";
	const char* const kSeatKey = "VirtualSeat";

	// AlternateFLH0..AlternateFLH4. GetFLH() itself refuses any index above 5
	// (`cmp esi, 5` at 0x6F3AFB), so a sixth seat could never have a port.
	const int kMaxSeats = 5;
	const size_t kValueMax = 512;

	// The key prefix virtual seats use for their gun ports - deliberately NOT
	// "AlternateFLH".
	//
	// Those five ports belong to the carrier's REAL passengers: the engine hands
	// passenger i its transport's AlternateFLH{i-1}, and any index above 5 is
	// refused (cmp esi,5 @0x6F3AFB) by leaving the FLH at (0,0,0) - which fires
	// from the carrier's centre rather than failing, so the loss is silent.
	// Sharing them would mean that filling a transport with real troops erodes
	// its virtual seats one by one, and a fully loaded transport would have none
	// left. Virtual seats get a set of their own instead, and the GetFLH tail
	// hook routes ONLY ghost seats to it.
	const char* const kPortKeyFormat = "VirtualFLH%d";

	// ObjectTypeClass::ImageFile - the type's ART section name.
	const size_t kImageFileOffset = 0x1F8;

	bool g_enabled = false;

	// ON by default: the first run is meant to be read-only.
	bool g_dryRun = true;

	// TechnoType -> seat types, in the order written. Index i fires from
	// AlternateFLH{i}. A null entry is a seat that was declared but could not be
	// resolved, and is skipped.
	std::map<TechnoTypeClass*, std::vector<TechnoTypeClass*>> g_plans;

	// The objects this module created. Membership is how a ghost is told apart
	// from a real passenger, so no per-carrier marker (which would dangle when a
	// carrier dies) is needed.
	std::set<FootClass*> g_ghosts;

	// Ghost seat -> its gun port offset, read from the carrier type's ART when
	// the seat is created. Held per object so the FLH hook is a plain lookup.
	std::map<FootClass*, CoordStruct> g_ghostPorts;

	// The object each in-flight GetFLH call is for.
	//
	// The tail hook CANNOT use EBX for this. That register happens to hold `this`
	// in the engine's own implementation, but Phobos replaces this function from
	// 0x6F3AEB and is free to reuse EBX afterwards - which is exactly why routing
	// ghost seats at the tail silently did nothing: the VirtualFLH values were
	// read correctly (they appear in the log) and shots still left the old ports.
	// The object is therefore captured at the function head, where ECX is
	// unambiguous, and read back at the tail.
	//
	// A fixed array rather than a vector: this runs for every GetFLH call in the
	// game, so it must not allocate. 0x6F3D50 is the function's only exit
	// (`ret 0x14` occurs once), so pushes and pops stay balanced.
	const int kFlhDepth = 8;
	TechnoClass* g_flhStack[kFlhDepth] = { nullptr };
	int g_flhDepth = 0;

	// Diagnostics. The hook and OnFrame both run on the game thread, so no
	// locking is needed.
	int g_reportedParsed = 0;
	int g_populatedCarriers = 0;
	int g_reportedPopulated = 0;
	int g_seatsCreated = 0;
	int g_reportedSeats = 0;
	int g_ghostSeatsSeen = 0;
	bool g_dryRunNoticeLogged = false;

	char* SkipSpace(char* p)
	{
		while (*p == ' ' || *p == '\t')
			++p;
		return p;
	}

	// Counts how many VirtualFLH0..4 a section actually defines.
	int CountPorts(CCINIClass* pArt, const char* section)
	{
		if (!section || *section == '\0' || !Karst::MemorySafety::IsReadable(section))
			return 0;

		int ports = 0;

		for (int i = 0; i < kMaxSeats; ++i)
		{
			char key[32] = {0};
			::sprintf_s(key, kPortKeyFormat, i);
			if (pArt->Exists(section, key))
				++ports;
		}

		return ports;
	}

	// Reads one port offset. The engine's own ART reader uses Read3Integers for
	// AlternateFLH (0x715FE1 `call 0x529CA0`), so the same call reads ours - a
	// CoordStruct is three integers.
	CoordStruct ReadPortOffset(CCINIClass* pArt, const char* section, int seat)
	{
		CoordStruct port = CoordStruct::Empty;

		char key[32] = {0};
		::sprintf_s(key, kPortKeyFormat, seat);

		int values[3] = {0, 0, 0};
		int defaults[3] = {0, 0, 0};

		if (pArt->Read3Integers(values, section, key, defaults))
		{
			port.X = values[0];
			port.Y = values[1];
			port.Z = values[2];
		}

		return port;
	}

	// DIAGNOSTIC: report how the engine actually stored this section.
	//
	// An earlier version of this file assumed INIEntry::Key/Value were plain
	// char* (as YRpp declares them) and compared Key with _strcmpi. Disassembly
	// of the engine's own readers says otherwise - ReadString @0x528AE8 does
	// `mov esi,[esi+0x10] / add esi,4 / mov esi,[esi] / mov ecx,[esi+0x10]`,
	// i.e. it dereferences Value twice before reaching characters - so that
	// comparison could never have matched, and the code silently fell back to
	// ReadString.
	//
	// Nothing is dereferenced here on a guess: the entry pointers themselves are
	// reported as numbers, which is enough to tell whether the section list is
	// walkable at all and how many entries the engine keeps for this key.
	void ProbeSectionStructure(CCINIClass* pINI, const char* section)
	{
		INIClass::INISection* pSection = pINI->GetSection(section);
		if (!pSection)
		{
			Karst::LogDebug(L"VirtualSeat[probe]: GetSection('%hs') returned null.", section);
			return;
		}

		GenericList* pList = &pSection->Entries;
		GenericNode* pNode = pList->First.Next;

		int total = 0;
		int shown = 0;

		for (int guard = 0; guard < 512 && pNode; ++guard)
		{
			if (pNode == &pList->Last || !Karst::MemorySafety::IsReadable(pNode))
				break;

			++total;
			INIClass::INIEntry* pEntry = static_cast<INIClass::INIEntry*>(pNode);

			if (shown < 3)
			{
				Karst::LogDebug(L"VirtualSeat[probe]: entry %d Key=%p Value=%p",
					total,
					static_cast<const void*>(pEntry->Key),
					static_cast<const void*>(pEntry->Value));
				++shown;
			}

			pNode = pNode->Next;
		}

		Karst::LogDebug(L"VirtualSeat[probe]: [%hs] section list walked, %d entr(ies).",
			section, total);
	}

	// A seat has to be able to BE a passenger, which only the FootClass family
	// can - a BuildingType can be named but could never join the list.
	//
	// Both spellings are accepted (the object kind and the type kind) because
	// which one a type object reports is not worth guessing at: a building is
	// rejected either way, and nothing else can be constructed into a
	// Passengers list.
	bool IsPassengerCapable(AbstractType what)
	{
		switch (what)
		{
		case AbstractType::Infantry:
		case AbstractType::InfantryType:
		case AbstractType::Unit:
		case AbstractType::UnitType:
		case AbstractType::Aircraft:
		case AbstractType::AircraftType:
			return true;
		default:
			return false;
		}
	}

	// Only these can carry passengers at all, which is what the ghost has to be
	// attached to.
	bool IsCarrier(TechnoClass* pTechno)
	{
		switch (pTechno->WhatAmI())
		{
		case AbstractType::Unit:
		case AbstractType::Aircraft:
			return true;
		default:
			return false;
		}
	}

	// Splits "E1,GGI, MTNK" into resolved types. Unresolvable or unusable
	// entries become a null slot (the seat stays empty) instead of shifting the
	// seats behind them, because the seat number IS the gun port.
	void ParseSeatList(char* value, std::vector<TechnoTypeClass*>& out,
		int& unknown, int& unusable, bool& overflow, std::string& report)
	{
		char* p = value;
		overflow = false;

		while (*p != '\0' && static_cast<int>(out.size()) < kMaxSeats)
		{
			p = SkipSpace(p);
			if (*p == '\0')
				break;

			char* start = p;
			while (*p != '\0' && *p != ',')
				++p;

			// Remember whether this token ended at a comma BEFORE the NUL is
			// written.
			//
			// This is the bug that turned "E1,GGI,GGI,MTNK,CARRIER" into one
			// seat: terminating the token first and testing `*p == ','`
			// afterwards cannot work, because the NUL lands exactly on the
			// comma whenever there is no trailing space to trim - so the test
			// always saw '\0', the cursor never advanced past it, and the loop
			// ended after the first token. Measured: 23 characters were read
			// correctly and exactly one seat came out.
			const bool more = (*p == ',');

			char* end = p;
			while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
				--end;
			*end = '\0';

			if (more)
				++p;

			if (*start == '\0')
			{
				out.push_back(nullptr);
				continue;
			}

			const int seat = static_cast<int>(out.size());
			TechnoTypeClass* pSeatType = TechnoTypeClass::Find(start);

			if (!pSeatType)
			{
				++unknown;
				out.push_back(nullptr);
				Karst::LogWarn(L"VirtualSeat: seat %d '%hs' is not a known type - "
					L"that seat is left empty.", seat, start);
				continue;
			}

			// A seat has to be able to BE a passenger, which only the FootClass
			// family can - a BuildingType can be named here but could never join
			// a Passengers list, so it is refused loudly rather than silently
			// producing an empty seat.
			if (!IsPassengerCapable(pSeatType->WhatAmI()))
			{
				++unusable;
				out.push_back(nullptr);
				Karst::LogError(L"VirtualSeat: seat %d '%hs' is not a passenger-capable "
					L"type - only infantry, vehicles and aircraft can be virtual seats; "
					L"buildings cannot. That seat is left empty.", seat, start);
				continue;
			}

			out.push_back(pSeatType);
			if (!report.empty())
				report += ", ";
			report += pSeatType->get_ID();
		}

		// Anything still unread once the loop stopped means the list was longer
		// than the engine can serve: GetFLH knows five ports and refuses any
		// index above that (`cmp esi, 5` @0x6F3AFB). Dropping the extras
		// silently would leave the author wondering why seat six never fires.
		p = SkipSpace(p);
		overflow = (*p != '\0');
	}

	// True once this carrier has at least one of OUR objects in its passenger
	// list. Walks the engine's own list (PassengersClass is a linked list
	// through ObjectClass::NextObject), so a carrier that legitimately carries
	// real passengers is still recognised correctly.
	bool HasGhostSeat(TechnoClass* pCarrier)
	{
		for (FootClass* p = pCarrier->Passengers.GetFirstPassenger();
			p != nullptr;
			p = static_cast<FootClass*>(p->NextObject))
		{
			if (g_ghosts.count(p) != 0)
				return true;
		}

		return false;
	}

	// One real, hidden object per seat, attached the way the engine attaches a
	// loaded passenger. The order follows the engine's own boarding code
	// (0x51A45E onwards): EnteredOpenTopped, then Transporter, then the
	// passenger list, then the motion is dropped.
	void SpawnSeats(TechnoClass* pCarrier, const std::vector<TechnoTypeClass*>& seats)
	{
		HouseClass* pOwner = pCarrier->Owner;
		if (!pOwner)
		{
			Karst::LogWarn(L"VirtualSeat: carrier %p has no owner - seats skipped.",
				static_cast<void*>(pCarrier));
			return;
		}

		for (size_t i = 0; i < seats.size(); ++i)
		{
			TechnoTypeClass* pSeatType = seats[i];
			if (!pSeatType)
				continue;

			ObjectClass* pCreated = pSeatType->CreateObject(pOwner);
			if (!pCreated)
			{
				Karst::LogWarn(L"VirtualSeat: seat %d could not create a '%hs' object.",
					static_cast<int>(i), pSeatType->get_ID());
				continue;
			}

			FootClass* pGhost = static_cast<FootClass*>(pCreated);

			pGhost->Location = pCarrier->Location;
			pGhost->InLimbo = true;
			pGhost->Transporter = pCarrier;

			// Marks it InOpenToppedTransport AND puts it on the logic layer -
			// without that second part a limbo object never runs its AI and
			// would never fire.
			pCarrier->EnteredOpenTopped(pGhost);

			// The engine's own list: PassengersClass::IndexOf is what the
			// engine turns into the AlternateFLH index, so the seat number has
			// to come from this list and nothing else.
			pCarrier->Passengers.AddPassenger(pGhost);
			pGhost->AbortMotion();

			g_ghosts.insert(pGhost);

			// Its own gun port, read once here so the FLH hook is a plain
			// lookup and never touches the INI.
			CoordStruct port = CoordStruct::Empty;

			CCINIClass* pArt =
				Karst::MemorySafety::SafeReadSlot<CCINIClass*>(&CCINIClass::INI_Art);

			if (pArt)
			{
				TechnoTypeClass* pCarrierType =
					static_cast<TechnoTypeClass*>(pCarrier->GetType());

				if (pCarrierType && pCarrierType->get_ID())
					port = ReadPortOffset(pArt, pCarrierType->get_ID(), static_cast<int>(i));
			}

			g_ghostPorts[pGhost] = port;
			++g_seatsCreated;

			Karst::LogInfo(L"VirtualSeat: seat %d on carrier %p <- created '%hs' (%p), "
				L"port (%d,%d,%d).",
				static_cast<int>(i), static_cast<void*>(pCarrier),
				pSeatType->get_ID(), static_cast<void*>(pGhost),
				port.X, port.Y, port.Z);
		}
	}

	// -----------------------------------------------------------------------
	// Seat count versus gun ports
	// -----------------------------------------------------------------------
	struct SeatVerdict
	{
		int  seats = 0;
		int  ports = 0;
		bool ok = false;
	};

	// One verdict per type: the ART INI does not change while a match runs, and
	// a rejected type must not be re-reported every single frame.
	std::map<TechnoTypeClass*, SeatVerdict> g_verdicts;

	bool CheckSeatCount(TechnoTypeClass* pType, int seats, SeatVerdict& verdict)
	{
		const auto cached = g_verdicts.find(pType);
		if (cached != g_verdicts.end())
		{
			verdict = cached->second;
			return verdict.ok;
		}

		verdict.seats = seats;
		verdict.ports = 0;
		verdict.ok = false;

		CCINIClass* pArt =
			Karst::MemorySafety::SafeReadSlot<CCINIClass*>(&CCINIClass::INI_Art);

		if (!pArt || !Karst::MemorySafety::IsReadable(pArt))
		{
			Karst::LogWarn(L"VirtualSeat: [%hs] declares %d seat(s) but the ART INI is not "
				L"available yet, so the gun ports cannot be counted - no seats this frame.",
				pType->get_ID(), seats);
			return false;
		}

		// The ports are counted in the type's OWN section, NOT in ImageFile.
		//
		// Measured: the engine reads AlternateFLH while ImageFile still holds the
		// type's ID - `Image=` is only consumed later, by the ART loader at
		// 0x45F230, which the ART reader calls after it has read its keys (see
		// the notes in src\dllmain.cpp). By the time OnFrame runs, ImageFile has
		// long been rewritten to whatever `Image=` said (BFRT declares
		// Image=SREF), so counting there would count the WRONG section and
		// report a mismatch that does not exist.
		//
		// ImageFile is still tried as a fallback, for a type that really does
		// relocate its ports along with its art.
		const char* pSection = pType->get_ID();

		if (!pSection || *pSection == '\0')
		{
			Karst::LogWarn(L"VirtualSeat: a type with no ID cannot be checked for gun "
				L"ports - no seats for it.");
			g_verdicts[pType] = verdict;
			return false;
		}

		verdict.ports = CountPorts(pArt, pSection);

		if (verdict.ports == 0)
		{
			const char* pImage =
				reinterpret_cast<const char*>(pType) + kImageFileOffset;

			if (pImage && *pImage && _strcmpi(pImage, pSection) != 0
				&& Karst::MemorySafety::IsReadable(pImage))
			{
				const int imagePorts = CountPorts(pArt, pImage);

				if (imagePorts > 0)
				{
					verdict.ports = imagePorts;
					pSection = pImage;
				}
			}
		}

		// Reported, NOT enforced. One seat per port is the ideal, but a mismatch
		// is a cosmetic problem: refusing to build over it made the feature look
		// broken and prevented the ports from ever being tested.
		verdict.ok = (seats == verdict.ports);

		if (seats == verdict.ports)
		{
			Karst::LogInfo(L"VirtualSeat: [%hs] %d seat(s) match %d gun port(s) in ART "
				L"section '%hs'.", pType->get_ID(), seats, verdict.ports, pSection);
		}
		else if (seats > verdict.ports)
		{
			Karst::LogWarn(L"VirtualSeat: [%hs] declares %d seat(s) but its ART section "
				L"'%hs' defines only %d VirtualFLH port(s) - seat(s) %d.. have no muzzle "
				L"and will fire from the carrier's centre. Add VirtualFLH%d= or shorten "
				L"VirtualSeat=.",
				pType->get_ID(), seats, pSection, verdict.ports, verdict.ports + 1,
				verdict.ports);
		}
		else
		{
			Karst::LogWarn(L"VirtualSeat: [%hs] declares %d seat(s) but its ART section "
				L"'%hs' defines %d VirtualFLH port(s) - the spare port(s) stay unused. "
				L"Add seat(s) to VirtualSeat= or remove VirtualFLH%d.. to match.",
				pType->get_ID(), seats, pSection, verdict.ports, seats);
		}

		g_verdicts[pType] = verdict;
		return verdict.ok;
	}

	// Set once the whole plan set has been checked, so the sweep below runs a
	// single time per match.
	bool g_plansValidated = false;

	// Validates every declared type ONCE, as soon as the ART INI can be read.
	//
	// CheckSeatCount is also called per carrier, but a configuration mistake must
	// not wait for somebody to build the unit. Measured: a 3-seat / 5-port
	// mismatch was only reported 23 seconds after the rules were parsed - the
	// moment the first carrier appeared - which reads as "no error at all" while
	// the log sits still. Nothing here needs a carrier, so the whole set is
	// checked as soon as the ART data exists.
	void ValidateAllPlans()
	{
		if (g_plansValidated || g_plans.empty())
			return;

		CCINIClass* pArt =
			Karst::MemorySafety::SafeReadSlot<CCINIClass*>(&CCINIClass::INI_Art);

		// Not loaded yet. Try again next frame rather than announcing a false
		// "no ports" for every type.
		if (!pArt || !Karst::MemorySafety::IsReadable(pArt))
			return;

		g_plansValidated = true;

		for (const auto& plan : g_plans)
		{
			SeatVerdict verdict;
			CheckSeatCount(plan.first, static_cast<int>(plan.second.size()), verdict);
		}
	}

	// Takes one ghost out of the world the way the engine would: off the
	// carrier's passenger list, off the logic layer (so its AI stops running),
	// then freed through the engine's own deallocator - Memory.h's rule for
	// anything that came from Type->CreateObject().
	void DestroyGhost(FootClass* pCarrier, FootClass* pGhost)
	{
		pCarrier->RemovePassenger(pGhost);
		pGhost->Deactivate();
		g_ghosts.erase(pGhost);
		g_ghostPorts.erase(pGhost);
		GameDelete(pGhost);
	}
}

namespace Karst
{
	namespace Ext
	{
		namespace VirtualSeat
		{
			void Init()
			{
				// Not the engine's INI: this module's own switch lives in
				// karst.ini, like every other feature (src\Ext\README.md).
				const Karst::Config& config = KarstExtension::GetConfig();

				if (!config.Enabled())
				{
					Karst::LogInfo(L"VirtualSeat: Karst is disabled, feature off.");
					return;
				}

				g_enabled = config.Reader().GetBool(kSection, kEnabledKey, true);
				g_dryRun = config.Reader().GetBool(kSection, kDryRunKey, true);

				if (!g_enabled)
				{
					Karst::LogInfo(L"VirtualSeat: [VirtualSeat] Enabled=no - feature off.");
					return;
				}

				Karst::LogInfo(L"VirtualSeat: installed. DryRun=%ls - %ls.",
					g_dryRun ? L"yes" : L"no",
					g_dryRun
						? L"the log will say what WOULD be built; nothing is created"
						: L"virtual seats are created for carriers that declare VirtualSeat=");
			}

			void OnTypeRulesRead(void* pType, void* pINI, const char* section)
			{
				if (!g_enabled)
					return;

				TechnoTypeClass* pSeatOwner = reinterpret_cast<TechnoTypeClass*>(pType);
				CCINIClass* pReadFrom = reinterpret_cast<CCINIClass*>(pINI);

				if (!pSeatOwner || !pReadFrom || !section || *section == '\0')
					return;

				if (!Karst::MemorySafety::IsReadable(pSeatOwner)
					|| !Karst::MemorySafety::IsReadable(pReadFrom))
				{
					return;
				}

				// Read the engine's own way and log the value BEFORE anything
				// touches it.
				//
				// ParseSeatList writes NULs where the commas were, so a log
				// printed after it shows only the first entry - which is what
				// made the first run of this feature look like the READ had
				// truncated "E1,GGI,..." to "E1". Printing the raw string and
				// its length separates "the read is short" from "the split is
				// wrong", which is the only question worth asking here.
				char raw[kValueMax] = {0};
				pReadFrom->ReadString(section, kSeatKey, "", raw, sizeof(raw));

				if (raw[0] == '\0')
				{
					// No key. Removing a key and hot-reloading must forget the
					// plan, so an absent key erases rather than leaves stale
					// seats behind.
					g_plans.erase(pSeatOwner);
					return;
				}

				Karst::LogDebug(L"VirtualSeat[probe]: [%hs] %hs='%hs' (%d char(s))",
					section, kSeatKey, raw, static_cast<int>(::strlen(raw)));

				ProbeSectionStructure(pReadFrom, section);

				// Split a copy. The engine's buffer is not ours to write into -
				// ReadString hands back its internal string for the default-less
				// case, and stamping NULs through it would corrupt the INI.
				char work[kValueMax] = {0};
				::strncpy_s(work, raw, _TRUNCATE);

				std::vector<TechnoTypeClass*> seats;
				std::string report;
				int unknown = 0;
				int unusable = 0;
				bool overflow = false;

				ParseSeatList(work, seats, unknown, unusable, overflow, report);

				// Warned, not enforced: seats beyond the engine's five ports
				// simply have no muzzle of their own.
				if (overflow)
				{
					Karst::LogWarn(L"VirtualSeat: [%hs] lists more than %d seats, but the "
						L"engine only has %d gun ports - the extra seat(s) will have no "
						L"muzzle.", section, kMaxSeats, kMaxSeats);
				}

				if (unusable > 0)
				{
					Karst::LogError(L"VirtualSeat: [%hs] lists %d type(s) that cannot be "
						L"passengers (buildings and other non-FootClass types). NO seats "
						L"are created for this type - fix VirtualSeat= in rulesmd.ini.",
						section, unusable);
					g_plans.erase(pSeatOwner);
					return;
				}

				Karst::LogInfo(L"VirtualSeat: [%hs] -> %d seat(s) resolved: %hs",
					section, static_cast<int>(seats.size()), report.c_str());

				if (seats.empty())
				{
					Karst::LogWarn(L"VirtualSeat: [%hs] declares the key but no seat "
						L"could be resolved - ignored.", section);
					g_plans.erase(pSeatOwner);
					return;
				}

				g_plans[pSeatOwner] = seats;
			}

			void OnFrame()
			{
				if (!g_enabled)
					return;

				// Counted from the plan table rather than incremented per parse:
				// a hot reload re-reads the same section, so a counter reports
				// the same type twice (observed: "2 type(s)" for one declared
				// type after editing rulesmd.ini while the game ran).
				const int declared = static_cast<int>(g_plans.size());
				if (declared != g_reportedParsed)
				{
					g_reportedParsed = declared;
					Karst::LogInfo(L"VirtualSeat: %d type(s) declare VirtualSeat=.",
						declared);
				}

				if (g_plans.empty())
					return;

				// Report every misconfigured seat list NOW - once the ART data is
				// readable - instead of waiting for a carrier to show up.
				ValidateAllPlans();

				DynamicVectorClass<TechnoClass*>* const pool = TechnoClass::Array;
				if (!pool || !Karst::MemorySafety::IsReadable(pool))
					return;

				for (TechnoClass* pTechno : *pool)
				{
					if (!pTechno || !Karst::MemorySafety::IsReadable(pTechno))
						continue;

					TechnoTypeClass* pType =
						static_cast<TechnoTypeClass*>(pTechno->GetType());
					if (!pType)
						continue;

					const auto plan = g_plans.find(pType);
					if (plan == g_plans.end())
						continue;

					// Only what is really on the map, and only what can carry
					// passengers at all.
					if (pTechno->InLimbo || !IsCarrier(pTechno))
						continue;

					if (HasGhostSeat(pTechno))
					{
						// Already populated: keep the seats where the carrier
						// is. The engine does this when a passenger boards, but
						// a ghost is never boarded, so it is done here.
						if (!g_dryRun)
						{
							static_cast<FootClass*>(pTechno)->UpdatePassengerCoords();
							++g_ghostSeatsSeen;
						}
						continue;
					}

					// Reported, NOT enforced. A seat/port mismatch is worth knowing
					// about, but refusing to build made a cosmetic mistake look
					// like a broken feature - and it stopped the VirtualFLH ports
					// from ever being exercised at all.
					SeatVerdict verdict;
					CheckSeatCount(pType, static_cast<int>(plan->second.size()), verdict);

					if (g_dryRun)
					{
						if (!g_dryRunNoticeLogged)
						{
							g_dryRunNoticeLogged = true;
							Karst::LogInfo(L"VirtualSeat: [DryRun] carrier %p of type '%hs' "
								L"is on the map with %d declared seat(s) - nothing was "
								L"created. Set [VirtualSeat] DryRun=no in karst.ini to "
								L"build them.", static_cast<void*>(pTechno),
								pType->get_ID(), static_cast<int>(plan->second.size()));
						}
						continue;
					}

					SpawnSeats(pTechno, plan->second);
					++g_populatedCarriers;
				}

				if (g_populatedCarriers != g_reportedPopulated
					|| g_seatsCreated != g_reportedSeats)
				{
					g_reportedPopulated = g_populatedCarriers;
					g_reportedSeats = g_seatsCreated;
					Karst::LogInfo(L"VirtualSeat: %d carrier(s) populated, %d seat object(s) "
						L"created.", g_populatedCarriers, g_seatsCreated);
				}
			}

			void OnCarrierPassengersExiting(void* pCarrierV)
			{
				if (!g_enabled || g_ghosts.empty())
					return;

				TechnoClass* pTechno = reinterpret_cast<TechnoClass*>(pCarrierV);
				if (!pTechno || !Karst::MemorySafety::IsReadable(pTechno))
					return;

				if (!IsCarrier(pTechno))
					return;

				// Collected first, removed afterwards: the passenger list is
				// walked here, and removing an entry mid-walk would cut the walk
				// short and strand the rest.
				std::vector<FootClass*> doomed;

				for (FootClass* p = pTechno->Passengers.GetFirstPassenger();
					p != nullptr;
					p = static_cast<FootClass*>(p->NextObject))
				{
					if (g_ghosts.count(p) != 0)
						doomed.push_back(p);
				}

				if (doomed.empty())
					return;

				Karst::LogInfo(L"VirtualSeat: carrier %p is being destroyed with %d ghost "
					L"seat(s) aboard - removing them, so none of them is unlimboed onto "
					L"the ground.", pCarrierV, static_cast<int>(doomed.size()));

				FootClass* pCarrier = static_cast<FootClass*>(pTechno);

				for (FootClass* pGhost : doomed)
					DestroyGhost(pCarrier, pGhost);
			}

			void PushFLHCall(void* pThis)
			{
				if (g_flhDepth < kFlhDepth)
					g_flhStack[g_flhDepth++] = reinterpret_cast<TechnoClass*>(pThis);
			}

			// Called from the GetFLH tail (0x6F3D50), the function's only exit.
			//
			// The object captured at the head is the CARRIER, not the passenger: a
			// passenger fires through its transport's ports, so the engine asks the
			// TRANSPORT for them - `pTransporter->GetFLH(-Passengers.IndexOf(pThis))`.
			// Measured: with five ghost seats aboard, `this` was the carrier's own
			// address on every call.
			//
			// The passenger is therefore identified through `idxWeapon`, which is
			// negative and whose magnitude is that 1-based passenger number. Only
			// when the numbered passenger turns out to be one of our ghosts is the
			// result rewritten; a real passenger keeps whatever the engine (or
			// Phobos) computed.
			void PopAndFixFLH(int idxWeapon, void* pDestV)
			{
				if (g_flhDepth <= 0)
					return;

				TechnoClass* pCarrier = g_flhStack[--g_flhDepth];
				g_flhStack[g_flhDepth] = nullptr;

				CoordStruct* pDest = reinterpret_cast<CoordStruct*>(pDestV);
				if (!pCarrier || !pDest)
					return;

				// A non-negative index is an ordinary weapon slot. Nothing to do.
				if (idxWeapon >= 0)
					return;

				if (g_ghostPorts.empty())
					return;

				const int wanted = -idxWeapon;
				int seen = 0;
				FootClass* pPassenger = nullptr;

				for (FootClass* p = pCarrier->Passengers.GetFirstPassenger();
					p != nullptr;
					p = static_cast<FootClass*>(p->NextObject))
				{
					if (++seen == wanted)
					{
						pPassenger = p;
						break;
					}
				}

				if (!pPassenger)
					return;

				const auto port = g_ghostPorts.find(pPassenger);

				static int s_flhProbes = 0;
				if (s_flhProbes < 30)
				{
					++s_flhProbes;
					Karst::LogDebug(L"VirtualSeat[flh]: probe %d carrier=%p slot=%d "
						L"passenger=%p ghosts=%d hit=%d",
						s_flhProbes, static_cast<void*>(pCarrier), wanted,
						static_cast<void*>(pPassenger),
						static_cast<int>(g_ghostPorts.size()),
						port != g_ghostPorts.end() ? 1 : 0);
				}

				if (port == g_ghostPorts.end())
					return;

				// The port is a carrier-relative offset, exactly like AlternateFLH
				// is for a real passenger. No turret or facing transform is
				// applied: a virtual seat is not a turret, and the engine has
				// already applied the base-coordinate offset it was handed before
				// this tail runs.
				pDest->X = pCarrier->Location.X + port->second.X;
				pDest->Y = pCarrier->Location.Y + port->second.Y;
				pDest->Z = pCarrier->Location.Z + port->second.Z;
			}

			void ResetForNewMatch()
			{
				// The engine rebuilds its objects every match, so every pointer
				// held here is stale the moment a new match starts.
				g_plans.clear();
				g_ghosts.clear();
				g_ghostPorts.clear();
				g_verdicts.clear();
				g_flhDepth = 0;
				g_plansValidated = false;
				g_reportedParsed = 0;
				g_populatedCarriers = 0;
				g_reportedPopulated = 0;
				g_seatsCreated = 0;
				g_reportedSeats = 0;
				g_ghostSeatsSeen = 0;
				g_dryRunNoticeLogged = false;
			}
		}
	}
}
