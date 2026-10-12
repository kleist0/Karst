#pragma once

#include <Windows.h>

// Safe readers for the engine pointers YRpp exposes.
//
// YRpp uses two different binding macros, and they behave very differently:
//
//   ALIAS(Type, Obj, Addr)    -> `Type &Obj = *reinterpret_cast<Type*>(Addr);`
//                                Obj IS the storage at Addr. Reading Obj pokes
//                                Addr directly, so a plain `if (Obj)` is a wild
//                                read whenever Addr is unmapped. Taking its
//                                address yields Addr itself.
//
//   ALIAS_O(Type, Obj, Addr)  -> `Type Obj = reinterpret_cast<Type>(Addr);`
//                                Obj is a variable inside the extension DLL
//                                whose VALUE is Addr. Reading Obj is always
//                                safe; the pointer it yields still has to be
//                                validated before use. Taking its address yields
//                                an address inside the DLL, not Addr.
//
// Concretely, for this YRpp copy:
//
//   ALIAS(HouseClass *, HouseClass::Player, 0xA83D4C);            // reference
//   ALIAS_O(MessageListClass * const, MessageListClass::Instance,
//           0xA8BC60);                                            // DLL variable
//
// So `if (HouseClass::Player)` crashes outside gamemd.exe, while
// `if (MessageListClass::Instance)` merely yields an unvalidated pointer.
// Both must go through these helpers.
//
// IMPORTANT LIMIT: these helpers answer "is this address mapped and readable".
// That is not the same as "is this a live engine object". Outside gamemd.exe,
// or before the engine publishes a global, a fixed address can hold unrelated
// mapped memory: the readability check passes, the pointer survives validation,
// and the first virtual call through it still crashes. Measured example - in a
// plain 32-bit test process the slot at 0xA8BC60 was readable and held
// 0x000097D4, which VirtualQuery also reported as readable, yet dereferencing it
// faulted.
//
// Therefore: use these helpers to avoid gratuitous wild reads, but still refuse
// to call into the engine until the engine itself has given you a reason to
// believe it is initialised.

namespace Karst
{
	namespace MemorySafety
	{
		// True when the whole range [address, address + size) is readable.
		bool IsReadable(const void* address, size_t size);

		// Pointer-sized readable check.
		bool IsReadable(const void* address);

		// Validates a pointer value before anyone dereferences it. Returns
		// nullptr when the value is not readable. Use this for values read from
		// an ALIAS_O variable:
		//     SafeValidate(MessageListClass::Instance)
		template <typename T>
		inline T* SafeValidate(const T* value)
		{
			return IsReadable(value) ? const_cast<T*>(value) : nullptr;
		}

		// Reads an ALIAS reference safely. `slot` is the address of the storage,
		// i.e. the Addr the reference is bound to, so the value is only loaded
		// after the address has been proven readable:
		//     SafeReadReference(&HouseClass::Player)
		template <typename T>
		inline T* SafeReadReference(T* const* slot)
		{
			if (!IsReadable(slot))
				return nullptr;

			T* value = *slot;
			return IsReadable(value) ? value : nullptr;
		}

		// Generic "read a pointer from this address" used for engine globals.
		//
		// Written as a template over the slot type so both `T*` and `T* const`
		// storage work, which is what YRpp's ALIAS and ALIAS_O produce
		// respectively:
		//
		//     SafeReadSlot<BuildingTypeClass*>(&BuildingTypeClass::Array)
		//     SafeReadSlot<MessageListClass*>(&MessageListClass::Instance)
		//
		// The slot address is tested before the load, so an unmapped engine
		// global yields nullptr instead of faulting. The pointer itself is
		// validated too; see the caveat at the top of this header about what
		// that does and does not guarantee.
		template <typename PtrT, typename SlotT>
		inline PtrT SafeReadSlot(SlotT* slot)
		{
			if (!IsReadable(static_cast<const void*>(slot)))
				return nullptr;

			PtrT value = *reinterpret_cast<PtrT const*>(slot);
			return IsReadable(value) ? value : nullptr;
		}
	}
}
