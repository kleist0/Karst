// Same as smoke_test.cpp, but it reports WHERE a fault happened instead of just
// that one did.
//
// The plain harness prints "CRASH at step 3" and stops, which is not enough to
// act on: the fault could be in this DLL, in the reported engine address, or in
// anything the step called. This one keeps the exception record (faulting
// address and code) and, when dbghelp is available, the symbol for that address
// inside the module that contains it.
//
// Build (32-bit) and run next to Karst.dll + karst.ini + karst.hotreload.ini:
//     cl /nologo /EHsc /Fe:smoke_where.exe smoke_where.cpp
//
// Exit code 0 means every step ran without faulting.

#include <Windows.h>
#include <dbghelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

static int g_step = 0;
static char g_stepName[128] = { 0 };
static DWORD g_faultAddress = 0;
static DWORD g_faultCode = 0;

static const char* ModuleOf(DWORD address, char* buffer, size_t size)
{
	HMODULE module = nullptr;
	if (!::GetModuleHandleExA(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(address), &module))
	{
		return "?";
	}

	char path[MAX_PATH] = { 0 };
	::GetModuleFileNameA(module, path, MAX_PATH);

	const char* name = path;
	for (const char* p = path; *p; ++p)
	{
		if (*p == '\\' || *p == '/')
			name = p + 1;
	}

	_snprintf_s(buffer, size, _TRUNCATE, "%s+0x%lX", name,
		address - reinterpret_cast<DWORD>(module));

	return buffer;
}

static void Report(const char* label)
{
	char module[256] = { 0 };
	printf("     %s: 0x%08lX in %s\n", label, g_faultAddress,
		ModuleOf(g_faultAddress, module, sizeof(module)));

	// Best-effort symbol, if a PDB for that module is around.
	HANDLE process = ::GetCurrentProcess();
	::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	if (::SymInitialize(process, nullptr, TRUE))
	{
		BYTE storage[sizeof(SYMBOL_INFO) + 512] = { 0 };
		SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 511;

		DWORD64 displacement = 0;
		if (::SymFromAddr(process, g_faultAddress, &displacement, symbol))
			printf("     symbol: %s+0x%llX\n", symbol->Name, displacement);

		::SymCleanup(process);
	}
}

static bool Guarded(void (*call)())
{
	__try
	{
		call();
		return true;
	}
	__except (
		g_faultCode = ::GetExceptionCode(),
		g_faultAddress = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(
			reinterpret_cast<EXCEPTION_POINTERS*>(_exception_info())->
				ExceptionRecord->ExceptionAddress)),
		EXCEPTION_EXECUTE_HANDLER)
	{
		printf("  !! FAULT in step %d (%s): exception 0x%08lX\n",
			g_step, g_stepName, g_faultCode);
		Report("faulting address");
		fflush(stdout);
		return false;
	}
}

static HMODULE g_dll = nullptr;

static void StepLoadLibrary() { g_dll = ::LoadLibraryA("Karst.dll"); }

static DWORD CallHook(const char* name)
{
	typedef DWORD(__cdecl* HookFn)(void*);
	HookFn fn = reinterpret_cast<HookFn>(::GetProcAddress(g_dll, name));
	if (!fn)
		return 0;
	return fn(nullptr);
}

static void StepCallEntry() { volatile DWORD r = CallHook("Karst_EntryPoint"); (void)r; }
static void StepCallLoop()  { volatile DWORD r = CallHook("Karst_MainLoop"); (void)r; }
static void StepFreeLibrary() { ::FreeLibrary(g_dll); }

int main()
{
	printf("target: Karst.dll (enhanced fault reporting)\n\n");

	const struct
	{
		const char* Name;
		void (*Call)();
	} steps[] = {
		{ "LoadLibrary",      &StepLoadLibrary },
		{ "entry-point hook", &StepCallEntry   },
		{ "main-loop hook",   &StepCallLoop    },
		{ "FreeLibrary",      &StepFreeLibrary },
	};

	for (int i = 0; i < 4; ++i)
	{
		g_step = i + 1;
		strncpy_s(g_stepName, steps[i].Name, _TRUNCATE);

		printf("step %d/4 %-18s ... ", g_step, steps[i].Name);
		fflush(stdout);

		if (!Guarded(steps[i].Call))
			return g_step;

		printf("ok\n");
		fflush(stdout);
	}

	printf("\nALL STEPS PASSED\n");
	return 0;
}
