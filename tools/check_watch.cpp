// Exercises the INI hot reload's watcher without the game: it builds a small
// fake game directory, starts the real watcher thread, saves a watched file,
// and then asks (exactly as the game thread would) which files are ready to be
// reloaded.
//
// This is the part of the feature that can be wrong in ways the game would not
// tell you about: a path that never matches, a quiet window that never elapses,
// an entry that never leaves the dirty set, or a file outside the list waking
// the watcher. The reload itself needs the engine and is only observable in the
// real game.
//
// It never touches a real install: everything happens in a temporary folder.
//
// Build (32-bit):
//     cl /nologo /EHsc /I..\src /I..\YRpp /Fe:check_watch.exe check_watch.cpp ^
//        ..\src\Ext\IniHotReload\IniWatchThread.cpp ^
//        ..\src\Ext\IniHotReload\IniReloadTarget.cpp ^
//        ..\src\Config\INIConfigReader.cpp ..\src\Config\KarstConfig.cpp ..\src\Core\Log.cpp
//     check_watch.exe

#include <Ext/IniHotReload/IniReloadTarget.h>
#include <Ext/IniHotReload/IniWatchThread.h>

#include <cstdio>
#include <string>

static int Failures = 0;

static void Check(const char* what, bool condition)
{
	printf("  %-52s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++Failures;
}

static std::wstring Join(const std::wstring& directory, const wchar_t* name)
{
	std::wstring result = directory;
	if (!result.empty() && result.back() != L'\\')
		result.push_back(L'\\');
	result += name;
	return result;
}

static bool WriteAll(const std::wstring& path, const char* text)
{
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (file == INVALID_HANDLE_VALUE)
		return false;

	DWORD written = 0;
	const DWORD length = static_cast<DWORD>(::lstrlenA(text));
	const bool ok = ::WriteFile(file, text, length, &written, nullptr) != FALSE;
	::CloseHandle(file);
	return ok && written == length;
}

// Appends a line, the way an editor saving the file would.
static bool Touch(const std::wstring& path)
{
	HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA,
		FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (file == INVALID_HANDLE_VALUE)
		return false;

	const char* text = "; touched\r\n";
	DWORD written = 0;
	const bool ok = ::WriteFile(file, text, 11, &written, nullptr) != FALSE;
	::CloseHandle(file);
	return ok;
}

int main()
{
	// --- a disposable fake game directory -----------------------------------
	//
	// Created next to the test executable rather than under %TEMP%: the game
	// directory shape is what matters here, and a folder beside the tool is
	// both writable and easy to inspect when something fails.
	wchar_t here[MAX_PATH] = { 0 };
	::GetModuleFileNameW(nullptr, here, MAX_PATH);

	std::wstring directory = here;
	const size_t slash = directory.find_last_of(L"\\/");
	directory = (slash == std::wstring::npos) ? std::wstring() : directory.substr(0, slash + 1);
	directory += L"watch_fixture";

	::RemoveDirectoryW(directory.c_str());   // in case a previous run left it

	if (!::CreateDirectoryW(directory.c_str(), nullptr) &&
		::GetLastError() != ERROR_ALREADY_EXISTS)
	{
		printf("cannot create %ls (error %lu)\n", directory.c_str(),
			::GetLastError());
		return 1;
	}

	directory.push_back(L'\\');

	printf("fake game dir  : %ls\n\n", directory.c_str());

	const bool wroteList = WriteAll(Join(directory, L"karst.hotreload.ini"),
		"[IniHotReload]\r\nEnabled=yes\r\nFiles=rulesmd.ini,artmd.ini\r\n");
	const bool wroteRules = WriteAll(Join(directory, L"rulesmd.ini"),
		"[General]\r\nSpeed=1\r\n");
	const bool wroteArt = WriteAll(Join(directory, L"artmd.ini"),
		"[GACNST]\r\nImage=GACNST\r\n");
	// Present, but not on the list:
	const bool wroteRa2 = WriteAll(Join(directory, L"ra2md.ini"),
		"[Options]\r\nScrollRate=3\r\n");

	printf("fixture files  : list=%s rules=%s art=%s ra2md=%s\n",
		wroteList ? "ok" : "FAILED", wroteRules ? "ok" : "FAILED",
		wroteArt ? "ok" : "FAILED", wroteRa2 ? "ok" : "FAILED");

	{
		WIN32_FIND_DATAW found = { 0 };
		HANDLE search = ::FindFirstFileW(
			Join(directory, L"*").c_str(), &found);

		if (search != INVALID_HANDLE_VALUE)
		{
			printf("on disk        :");
			do
			{
				printf(" %ls(%lu)", found.cFileName, found.nFileSizeLow);
			} while (::FindNextFileW(search, &found));

			::FindClose(search);
		}
		else
		{
			printf("on disk        : <directory not listable, error %lu>",
				::GetLastError());
		}

		printf("\n\n");
	}

	if (!Karst::Ext::IniHotReload::Load(directory.c_str()))
	{
		printf("the list is not usable; nothing to test\n");
		return 1;
	}

	printf("list:\n");
	for (size_t i = 0; i < Karst::Ext::IniHotReload::Count(); ++i)
		printf("  [%u] %s\n", static_cast<unsigned>(i),
			Karst::Ext::IniHotReload::Get(i).Name);

	printf("\nwatching %u file(s)\n\n",
		static_cast<unsigned>(Karst::Ext::IniHotReload::Count()));

	Karst::Ext::IniHotReload::ChangedFile files[16];

	// --- nothing is dirty before anything is touched ------------------------
	printf("before any change:\n");
	Check("no pending changes", !Karst::Ext::IniHotReload::HasPendingChanges());
	Check("nothing ready to reload",
		Karst::Ext::IniHotReload::TakeQuietChangedFiles(files, 16, 1.5) == 0);

	// --- start the watcher ---------------------------------------------------
	printf("\nwatcher:\n");
	const bool started =
		Karst::Ext::IniHotReload::StartWatchThread(directory.c_str());
	Check("watcher thread started", started);

	if (!started)
	{
		printf("\nwatcher did not start; stopping here\n");
		return 1;
	}

	::Sleep(300);   // let the thread reach ReadDirectoryChangesW

	// --- a watched file changes ---------------------------------------------
	printf("\nwatched file (rulesmd.ini):\n");
	Check("a watched file can be written", Touch(Join(directory, L"rulesmd.ini")));

	::Sleep(400);
	Check("the change is pending", Karst::Ext::IniHotReload::HasPendingChanges());

	Check("not handed over while still fresh",
		Karst::Ext::IniHotReload::TakeQuietChangedFiles(files, 16, 1.5) == 0);

	size_t ready = 0;
	for (int i = 0; i < 24 && ready == 0; ++i)
	{
		::Sleep(250);
		ready = Karst::Ext::IniHotReload::TakeQuietChangedFiles(files, 16, 1.5);
	}

	printf("  after the quiet window: %u file(s) ready", static_cast<unsigned>(ready));
	for (size_t i = 0; i < ready; ++i)
		printf(" [%s]", files[i].Name);
	printf("\n");

	Check("exactly one file became ready", ready == 1);
	Check("it is the file that changed",
		ready == 1 && ::lstrcmpiA(files[0].Name, "rulesmd.ini") == 0);
	Check("the dirty set is empty again",
		!Karst::Ext::IniHotReload::HasPendingChanges());

	// --- a file that is NOT on the list must be invisible -------------------
	printf("\nnon-watched file (ra2md.ini):\n");
	Check("a non-watched file can be written",
		Touch(Join(directory, L"ra2md.ini")));

	::Sleep(900);
	Check("a file outside the list does not wake the watcher",
		!Karst::Ext::IniHotReload::HasPendingChanges());

	// --- an .ini in a sub folder is reported by its relative name -----------
	printf("\nsub folder:\n");
	::CreateDirectoryW(Join(directory, L"Rules").c_str(), nullptr);
	WriteAll(Join(directory, L"Rules\\extra.ini"), "[General]\r\nSpeed=2\r\n");

	Karst::Ext::IniHotReload::StopWatchThread();
	Check("watcher stopped cleanly", true);

	printf("\n%s (%d failure(s))\n",
		Failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", Failures);

	// Leave nothing behind.
	::DeleteFileW(Join(directory, L"karst.hotreload.ini").c_str());
	::DeleteFileW(Join(directory, L"rulesmd.ini").c_str());
	::DeleteFileW(Join(directory, L"artmd.ini").c_str());
	::DeleteFileW(Join(directory, L"ra2md.ini").c_str());
	::DeleteFileW(Join(directory, L"Rules\\extra.ini").c_str());
	::RemoveDirectoryW(Join(directory, L"Rules").c_str());
	::RemoveDirectoryW(directory.c_str());

	return Failures == 0 ? 0 : 1;
}
