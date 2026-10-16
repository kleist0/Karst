#pragma once

// "VirtualSeat" - gun ports that do not need real passengers.
//
// WHAT IT DOES
//
// A transport that declares `Passengers=N` + `OpenTopped=yes` can already carry
// N passengers which shoot out of it, each from its own gun port: the engine
// gives the i-th passenger the transport's `AlternateFLH{i-1}` as its muzzle.
// VirtualSeat copies that arrangement from a declaration, without anything ever
// being loaded:
//
//     rulesmd.ini - the transport's own section:
//         [BFRT]
//         Passengers=5
//         OpenTopped=yes
//         VirtualSeat=E1,GGI,GGI,MTNK,CARRIER
//
//     artmd.ini - the same section:
//         [BFRT]
//         AlternateFLH0=45,190,90
//         AlternateFLH1=45,-190,90
//         ...                        ; seat i fires from AlternateFLH{i-1}
//
// The declaration is independent of `Passengers=`: a transport may carry its
// real passengers AND have virtual seats at the same time.
//
// HOW THE PORT IS CHOSEN (measured in gamemd 1.001 - not guessed)
//
//     TechnoClass::GetFLH(CoordStruct* pDest, int idxWeapon, CoordStruct base)
//     @ 0x6F3AD0
//         0x6F3AF5  test esi, esi
//         0x6F3AF7  jge  0x6F3B23          ; idx >= 0 -> Weapon[idx].FLH
//         0x6F3AF9  neg  esi               ; idx = -idxWeapon
//         0x6F3AFB  cmp  esi, 5
//         0x6F3B0D  lea  edx, [eax + ecx*4 + 0x850]   ; Type + idx*12 + 0x850
//
//     so GetFLH(-1) is AlternateFLH0 (TechnoTypeClass + 0x85C = 0x850 + 12),
//     GetFLH(-2) is AlternateFLH1, and so on. That is exactly what the engine's
//     own passenger path uses: a passenger's GetFLH calls
//     `Transporter->GetFLH(-Passengers.IndexOf(this), ...)`, and
//     PassengersClass::IndexOf is 1-based. Seat i therefore fires from
//     AlternateFLH{i-1}.
//
// HOW THE SEATS ARE POPULATED
//
// By creating one real, hidden object per seat and attaching it the way the
// engine attaches a loaded passenger - Transporter, InOpenToppedTransport, the
// Passengers list, and the logic layer. That is deliberate: the engine then
// drives the seat's own AI (target scanning, ROF, ammo, elite weapon, firing
// animation, sound, burst) and computes the muzzle itself, so the behaviour is
// the stock one rather than an imitation of it.
//
// TWO STAGES, ON PURPOSE
//
//     karst.ini:
//         [VirtualSeat]
//         Enabled=yes
//         DryRun=yes      ; parse and log only - creates NOTHING
//
// DryRun defaults to ON. The feature makes the engine own objects the game did
// not ask for, so the first run is meant to be read-only: the log then says
// what would be built, and not one byte of game state changes.
//
// The engine classes are only ever passed as pointers here, so this header
// needs no engine include. They must sit at global scope: inside the namespaces
// below they would declare different types.

class CCINIClass;
class TechnoClass;
class TechnoTypeClass;

namespace Karst
{
	namespace Ext
	{
		namespace VirtualSeat
		{
			// Called once from KarstExtension::OnGameStart().
			void Init();

			// Called from the per-frame hook (KarstExtension::OnFrame).
			void OnFrame();

			// Called from KarstExtension::OnProcessDetach().
			void ResetForNewMatch();

			// Called from the hook at 0x7143BD, i.e. while the engine reads a
			// TechnoType's own section of the rules INI.
			//
			// `pType` is that TechnoTypeClass* (EBP), `pINI` the CCINIClass the
			// engine is reading from (ESI) and `section` the section name
			// (EBX, the type's ID) - the same three values the surrounding
			// engine code uses one instruction later to read `OpenTopped=`.
			//
			// Reading the key from that live INIClass rather than from the
			// CCINIClass::INI_Rules singleton is what makes a hot reload work:
			// a reload builds a temporary INIClass, and the singleton would
			// keep answering with the startup copy. Same reasoning as
			// Ext\BuildCost.
			void OnTypeRulesRead(void* pType, void* pINI, const char* section);

			// Called from the hook at 0x7104C0 - the engine's
			// TechnoClass::MarkPassengersAsExited, which runs while a carrier is
			// being destroyed and BEFORE the engine unlimbos its passengers onto
			// the ground. Ghost seats are taken off the passenger list and
			// destroyed here, so a dying carrier does not hand the player five
			// free units (measured: it did, before this existed).
			void OnCarrierPassengersExiting(void* pCarrier);

			// Called from the hook at 0x6F3AD0 - the HEAD of TechnoClass::GetFLH,
			// where ECX is unambiguously `this`. The object is remembered so the
			// tail hook can tell whose call it is finishing; EBX cannot be trusted
			// there once Phobos has replaced the function body.
			void PushFLHCall(void* pThis);

			// Called from the hook at 0x6F3D50 - the TAIL of TechnoClass::GetFLH,
			// where the engine's own implementation and Phobos' replacement both
			// converge (that address is Phobos' SkipGameCode).
			//
			// `idxWeapon` is the function's second argument: negative for a
			// passenger-port request, and its magnitude is the passenger's
			// 1-based position in the carrier's list. The captured object is the
			// CARRIER - a passenger fires through its transport's ports, so that
			// is who the engine asks. Only our ghost seats get rewritten.
			//
			// Taking void* rather than CoordStruct* keeps this header free of
			// engine includes.
			void PopAndFixFLH(int idxWeapon, void* pDest);
		}
	}
}
