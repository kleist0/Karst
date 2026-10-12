#include <Core/MemorySafety.h>

namespace Karst
{
	namespace MemorySafety
	{
		bool IsReadable(const void* address, size_t size)
		{
			if (!address || size == 0)
				return false;

			const BYTE* cursor = static_cast<const BYTE*>(address);
			const BYTE* const end = cursor + size;

			// Guard against wrapping past the top of the address space.
			if (end < cursor)
				return false;

			while (cursor < end)
			{
				MEMORY_BASIC_INFORMATION info = { 0 };
				if (::VirtualQuery(cursor, &info, sizeof(info)) != sizeof(info))
					return false;

				if (info.State != MEM_COMMIT)
					return false;

				// PAGE_GUARD makes the first touch raise an exception, and
				// PAGE_NOACCESS is obviously unusable.
				if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
					return false;

				const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
					PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

				if ((info.Protect & readable) == 0)
					return false;

				const BYTE* const regionEnd = cursor + info.RegionSize;
				cursor = (regionEnd > cursor) ? regionEnd : end;
			}

			return true;
		}

		bool IsReadable(const void* address)
		{
			return IsReadable(address, sizeof(void*));
		}
	}
}
