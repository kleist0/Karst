// Karst smoke test - loads Karst.dll and calls its exported hook entry points
// without launching the game.
//
// Why this exists: Syringe needs administrator rights, and in a managed
// environment it may refuse to run at all ("No or invalid command line
// arguments given"). Without the game there is otherwise no way to tell whether
// the DLL even loads. This harness exercises the whole non-engine path -
// DllMain, config loading, logger init, both exported hooks, and unload - and
// catches the mistakes that are easy to make in this kind of DLL: a wild read
// of an engine global, a bad format string, a broken detach path.
//
// It cannot validate Syringe's hook registration, and it says nothing about
// code that dereferences real gamemd.exe structures.
//
// Build (32-bit, must match the DLL):
//     cl /nologo /EHsc /Fe:smoke_test.exe smoke_test.cpp
//
// Run from the folder that holds Karst.dll and karst.ini:
//     smoke_test.exe
//
// Then inspect karst.log. A successful run ends with SMOKE TEST PASSED and a
// log that reports the loaded config and a first frame.

#include <Windows.h>
#include <cstdio>
#include <excpt.h>

static int g_step = 0;

// Runs `call` under SEH and reports an access violation instead of dying.
// Only calls that need no unwinding are passed in here.
static bool Guarded(void (*call)())
{
	__try
	{
		call();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		printf("  !! CRASH at step %d: exception 0x%08lX\n", g_step, ::GetExceptionCode());
		fflush(stdout);
		return false;
	}
}

static HMODULE g_dll = nullptr;

static void StepLoadLibrary()
{
	g_dll = ::LoadLibraryA("Karst.dll");
}

static DWORD CallHook(const char* name, bool* found)
{
	typedef DWORD(__cdecl* HookFn)(void*);
	HookFn fn = reinterpret_cast<HookFn>(::GetProcAddress(g_dll, name));

	*found = (fn != nullptr);
	if (!fn)
		return 0;

	// The hooks ignore their argument at this stage, so nullptr is safe.
	return fn(nullptr);
}

static void StepCallEntry()
{
	bool found = false;
	volatile DWORD result = CallHook("Karst_EntryPoint", &found);
	(void)result;
}

static void StepCallLoop()
{
	bool found = false;
	volatile DWORD result = CallHook("Karst_MainLoop", &found);
	(void)result;
}

static void StepFreeLibrary()
{
	::FreeLibrary(g_dll);
}

int main()
{
	printf("target: Karst.dll\n");
	fflush(stdout);

	g_step = 1;
	printf("step 1/4 LoadLibrary      ... ");
	fflush(stdout);
	if (!Guarded(StepLoadLibrary))
		return 1;
	if (!g_dll)
	{
		printf("FAILED, GetLastError=%lu\n", ::GetLastError());
		return 1;
	}
	printf("ok\n");
	fflush(stdout);

	g_step = 2;
	printf("step 2/4 entry-point hook ... ");
	fflush(stdout);
	if (!Guarded(StepCallEntry))
		return 2;
	printf("ok\n");
	fflush(stdout);

	g_step = 3;
	printf("step 3/4 main-loop hook   ... ");
	fflush(stdout);
	if (!Guarded(StepCallLoop))
		return 3;
	printf("ok\n");
	fflush(stdout);

	g_step = 4;
	printf("step 4/4 FreeLibrary      ... ");
	fflush(stdout);
	if (!Guarded(StepFreeLibrary))
		return 4;
	printf("ok\n");
	fflush(stdout);

	printf("\nSMOKE TEST PASSED\n");
	fflush(stdout);
	return 0;
}
