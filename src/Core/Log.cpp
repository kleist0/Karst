#include <Core/Log.h>

#include <Version.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Karst
{
	namespace Log
	{
		namespace
		{
			CRITICAL_SECTION Lock;
			bool LockReady = false;

			LogLevel CurrentLevel = LogLevel::Info;
			bool FileLoggingEnabled = false;

			HANDLE LogFile = INVALID_HANDLE_VALUE;
			std::wstring LogPath;

			const wchar_t* LevelTag(LogLevel level)
			{
				switch (level)
				{
				case LogLevel::Error: return L"[ERROR]";
				case LogLevel::Warn:  return L"[WARN ]";
				case LogLevel::Info:  return L"[INFO ]";
				case LogLevel::Debug: return L"[DEBUG]";
				default:              return L"[     ]";
				}
			}

			// Class guard: takes the lock when it was created, releases it on scope exit.
			class ScopedLock
			{
			public:
				ScopedLock()
				{
					if (LockReady)
						::EnterCriticalSection(&Lock);
				}

				~ScopedLock()
				{
					if (LockReady)
						::LeaveCriticalSection(&Lock);
				}

				ScopedLock(const ScopedLock&) = delete;
				ScopedLock& operator=(const ScopedLock&) = delete;
			};

			void EnsureLock()
			{
				if (!LockReady)
				{
					::InitializeCriticalSection(&Lock);
					LockReady = true;
				}
			}

			std::wstring BuildLogPath()
			{
				wchar_t gameDirectory[MAX_PATH] = { 0 };
				if (::GetModuleFileNameW(nullptr, gameDirectory, MAX_PATH) == 0)
					return std::wstring();

				wchar_t* lastSeparator = nullptr;
				for (wchar_t* cursor = gameDirectory; *cursor; ++cursor)
				{
					if (*cursor == L'\\' || *cursor == L'/')
						lastSeparator = cursor;
				}

				if (!lastSeparator)
					return std::wstring();

				*(lastSeparator + 1) = L'\0';

				std::wstring path = gameDirectory;
				path += L"karst.log";
				return path;
			}

			void OpenFileIfNeeded()
			{
				if (!FileLoggingEnabled || LogFile != INVALID_HANDLE_VALUE)
					return;

				LogPath = BuildLogPath();
				if (LogPath.empty())
					return;

				const DWORD attributes = ::GetFileAttributesW(LogPath.c_str());
				const bool isNew = (attributes == INVALID_FILE_ATTRIBUTES);

				LogFile = ::CreateFileW(
					LogPath.c_str(),
					FILE_APPEND_DATA,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr,
					OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL,
					nullptr);

				if (LogFile == INVALID_HANDLE_VALUE)
					return;

				if (isNew)
				{
					// Mark the start of a session so old and new runs are distinguishable.
					const char* banner = "\xEF\xBB\xBF"
						"=========================================================\n"
						" " KARST_NAME " " KARST_VERSION_STR " - log opened\n"
						"=========================================================\n";

					DWORD written = 0;
					::WriteFile(LogFile, banner, static_cast<DWORD>(std::strlen(banner)), &written, nullptr);
				}
			}

			void WriteLine(const wchar_t* text)
			{
				::OutputDebugStringW(KARST_NAME_WSTR L": ");
				::OutputDebugStringW(text);
				::OutputDebugStringW(L"\n");
			}

			void WriteLineToFile(const wchar_t* text)
			{
				OpenFileIfNeeded();
				if (LogFile == INVALID_HANDLE_VALUE)
					return;

				// Timestamp prefix.
				SYSTEMTIME time = { 0 };
				::GetLocalTime(&time);

				wchar_t stamped[1024] = { 0 };
				::swprintf_s(stamped, L"[%04u-%02u-%02u %02u:%02u:%02u] %ls\r\n",
					time.wYear, time.wMonth, time.wDay,
					time.wHour, time.wMinute, time.wSecond,
					text);

				// Store as UTF-8 so the file is readable in any editor.
				char utf8[2048] = { 0 };
				const int converted = ::WideCharToMultiByte(
					CP_UTF8, 0, stamped, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);

				if (converted > 1)
				{
					DWORD written = 0;
					::WriteFile(LogFile, utf8, static_cast<DWORD>(converted - 1), &written, nullptr);
				}
			}
		}

		void Init(const Config& config)
		{
			EnsureLock();
			ScopedLock guard;

			CurrentLevel = config.GetLogLevel();
			FileLoggingEnabled = config.DebugLog();
		}

		void Shutdown()
		{
			EnsureLock();
			ScopedLock guard;

			if (LogFile != INVALID_HANDLE_VALUE)
			{
				::CloseHandle(LogFile);
				LogFile = INVALID_HANDLE_VALUE;
			}

			CurrentLevel = LogLevel::Off;
			FileLoggingEnabled = false;
		}

		bool IsEnabled(LogLevel level)
		{
			if (CurrentLevel == LogLevel::Off)
				return false;

			return static_cast<int>(level) <= static_cast<int>(CurrentLevel);
		}

		void Write(LogLevel level, const wchar_t* format, ...)
		{
			if (!format)
				return;

			EnsureLock();
			ScopedLock guard;

			if (!IsEnabled(level))
				return;

			va_list args;
			va_start(args, format);

			wchar_t body[1024] = { 0 };
			::_vsnwprintf_s(body, _TRUNCATE, format, args);

			va_end(args);

			wchar_t line[1200] = { 0 };
			::swprintf_s(line, L"%ls %ls", LevelTag(level), body);

			WriteLine(line);
			WriteLineToFile(line);
		}

		void Write(LogLevel level, const char* format, ...)
		{
			if (!format)
				return;

			va_list args;
			va_start(args, format);

			char body[1024] = { 0 };
			::_vsnprintf_s(body, _TRUNCATE, format, args);

			va_end(args);

			// The narrow overload just funnels into the wide one.
			wchar_t wide[1024] = { 0 };
			if (::MultiByteToWideChar(CP_ACP, 0, body, -1, wide, 1024) == 0)
				return;

			Log::Write(level, L"%ls", wide);
		}
	}
}
