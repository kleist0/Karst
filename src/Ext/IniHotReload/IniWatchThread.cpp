#include <Ext/IniHotReload/IniWatchThread.h>

#include <Core/Log.h>
#include <Ext/IniHotReload/IniReloadTarget.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			namespace
			{
				// Notification buffer. Windows asks for a size that fits the
				// changes made since the last read; 64 KB covers a burst of
				// edits, and overflow is handled rather than ignored.
				const DWORD NotifyBufferSize = 64 * 1024;

				HANDLE           DirectoryHandle = INVALID_HANDLE_VALUE;
				HANDLE           StopEvent = nullptr;
				HANDLE           ThreadHandle = nullptr;
				DWORD            ThreadId = 0;

				std::mutex             DirtyMutex;
				std::map<std::wstring, double> DirtyFiles;   // full path -> last change

				std::atomic<bool>      DirtyFlag(false);
				std::atomic<bool>      ThreadRunning(false);

				std::wstring WatchedDirectory;

				double NowSeconds()
				{
					return static_cast<double>(::GetTickCount64()) / 1000.0;
				}

				std::wstring Lowercase(const std::wstring& text)
				{
					std::wstring result = text;
					for (wchar_t& c : result)
					{
						if (c >= L'A' && c <= L'Z')
							c = static_cast<wchar_t>(c - L'A' + L'a');
					}

					return result;
				}

				void MarkDirty(const std::wstring& fullPath)
				{
					// Cheap rejection first: the watcher sees every temp file,
					// every log write and every save in the game directory, and
					// only the configured handful matter.
					if (!Matches(fullPath))
						return;

					{
						std::lock_guard<std::mutex> guard(DirtyMutex);
						DirtyFiles[Lowercase(fullPath)] = NowSeconds();
					}

					DirtyFlag.store(true);
				}

				void MarkAllDirty()
				{
					const double now = NowSeconds();

					{
						std::lock_guard<std::mutex> guard(DirtyMutex);
						for (size_t i = 0; i < Count(); ++i)
							DirtyFiles[Get(i).FullPath] = now;
					}

					DirtyFlag.store(true);
				}

				// One entry of the FILE_NOTIFY_INFORMATION list.
				const wchar_t* NotifyName(const FILE_NOTIFY_INFORMATION* info)
				{
					return info->FileName;   // not NUL terminated
				}

				size_t NotifyNameLength(const FILE_NOTIFY_INFORMATION* info)
				{
					return info->FileNameLength / sizeof(wchar_t);
				}

				void HandleNotifications(const std::vector<BYTE>& buffer, DWORD bytes)
				{
					if (bytes == 0)
						return;

					const BYTE* cursor = buffer.data();
					const BYTE* end = buffer.data() + bytes;

					for (;;)
					{
						if (cursor + sizeof(FILE_NOTIFY_INFORMATION) > end)
							break;

						const FILE_NOTIFY_INFORMATION* info =
							reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);

						const bool interesting =
							info->Action == FILE_ACTION_ADDED ||
							info->Action == FILE_ACTION_MODIFIED ||
							info->Action == FILE_ACTION_RENAMED_NEW_NAME;

						if (interesting)
						{
							const std::wstring name(
								NotifyName(info), NotifyNameLength(info));

							if (!name.empty())
								MarkDirty(WatchedDirectory + name);
						}

						if (info->NextEntryOffset == 0)
							break;

						cursor += info->NextEntryOffset;
					}
				}

				DWORD WINAPI WatchThreadProc(LPVOID /*param*/)
				{
					ThreadRunning.store(true);

					std::vector<BYTE> buffer;
					try
					{
						buffer.resize(NotifyBufferSize);
					}
					catch (...)
					{
						LogError(L"IniHotReload: could not allocate the change buffer.");
						ThreadRunning.store(false);
						return 0;
					}

					OVERLAPPED overlapped = { 0 };
					overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

					if (!overlapped.hEvent)
					{
						LogError(L"IniHotReload: could not create the watch event.");
						ThreadRunning.store(false);
						return 0;
					}

					bool firstPass = true;

					for (;;)
					{
						::ResetEvent(overlapped.hEvent);

						const BOOL issued = ::ReadDirectoryChangesW(
							DirectoryHandle,
							buffer.data(),
							NotifyBufferSize,
							TRUE,                                   // recurse
							FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
								FILE_NOTIFY_CHANGE_FILE_NAME,
							nullptr,
							&overlapped,
							nullptr);

						if (!issued)
						{
							const DWORD error = ::GetLastError();
							LogError(L"IniHotReload: ReadDirectoryChangesW failed (%u), "
								L"watching is off.", error);
							break;
						}

						HANDLE waits[2] = { overlapped.hEvent, StopEvent };
						const DWORD waited = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);

						if (waited == WAIT_OBJECT_0 + 1)
						{
							// Asked to leave; cancel the pending read so the
							// overlapped structure stays valid for the cancel.
							::CancelIo(DirectoryHandle);
							::WaitForSingleObject(overlapped.hEvent, 2000);
							break;
						}

						if (waited != WAIT_OBJECT_0)
						{
							LogError(L"IniHotReload: wait failed (%u), watching is off.",
								::GetLastError());
							break;
						}

						DWORD transferred = 0;
						if (!::GetOverlappedResult(
								DirectoryHandle, &overlapped, &transferred, FALSE))
						{
							const DWORD error = ::GetLastError();

							// The buffer overflowed: changes were lost. Treat it
							// as "everything changed" rather than silently
							// missing the user's edit.
							if (error == ERROR_NOTIFY_ENUM_DIR)
							{
								LogWarn(L"IniHotReload: change buffer overflowed, "
									L"assuming every watched file changed.");
								MarkAllDirty();
								continue;
							}

							LogError(L"IniHotReload: GetOverlappedResult failed (%u), "
								L"watching is off.", error);
							break;
						}

						HandleNotifications(buffer, transferred);

						if (firstPass)
						{
							firstPass = false;
							LogDebug(L"IniHotReload: watcher is live on %ls.",
								WatchedDirectory.c_str());
						}
					}

					::CloseHandle(overlapped.hEvent);
					ThreadRunning.store(false);
					return 0;
				}
			}

			bool StartWatchThread(const wchar_t* gameDirectory)
			{
				if (ThreadRunning.load() || ThreadHandle)
				{
					LogWarn(L"IniHotReload: the watcher is already running.");
					return false;
				}

				if (!gameDirectory || !*gameDirectory)
					return false;

				WatchedDirectory = gameDirectory;
				if (WatchedDirectory.back() != L'\\' && WatchedDirectory.back() != L'/')
					WatchedDirectory.push_back(L'\\');

				DirectoryHandle = ::CreateFileW(
					WatchedDirectory.c_str(),
					FILE_LIST_DIRECTORY,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					nullptr,
					OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
					nullptr);

				if (DirectoryHandle == INVALID_HANDLE_VALUE)
				{
					LogError(L"IniHotReload: cannot watch %ls (%u).",
						WatchedDirectory.c_str(), ::GetLastError());
					return false;
				}

				StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
				if (!StopEvent)
				{
					LogError(L"IniHotReload: could not create the stop event.");
					::CloseHandle(DirectoryHandle);
					DirectoryHandle = INVALID_HANDLE_VALUE;
					return false;
				}

				ThreadHandle = ::CreateThread(
					nullptr, 0, &WatchThreadProc, nullptr, 0, &ThreadId);

				if (!ThreadHandle)
				{
					LogError(L"IniHotReload: could not start the watcher thread.");
					::CloseHandle(StopEvent);
					StopEvent = nullptr;
					::CloseHandle(DirectoryHandle);
					DirectoryHandle = INVALID_HANDLE_VALUE;
					return false;
				}

				LogInfo(L"IniHotReload: watching %ls for changes.", WatchedDirectory.c_str());
				return true;
			}

			void StopWatchThread()
			{
				if (StopEvent)
					::SetEvent(StopEvent);

				if (ThreadHandle)
				{
					// The thread cancels its pending read and returns; a bounded
					// wait keeps a stuck thread from hanging process detach.
					::WaitForSingleObject(ThreadHandle, 3000);
					::CloseHandle(ThreadHandle);
					ThreadHandle = nullptr;
				}

				if (StopEvent)
				{
					::CloseHandle(StopEvent);
					StopEvent = nullptr;
				}

				if (DirectoryHandle != INVALID_HANDLE_VALUE)
				{
					::CloseHandle(DirectoryHandle);
					DirectoryHandle = INVALID_HANDLE_VALUE;
				}

				{
					std::lock_guard<std::mutex> guard(DirtyMutex);
					DirtyFiles.clear();
				}

				DirtyFlag.store(false);
				ThreadRunning.store(false);
			}

			bool HasPendingChanges()
			{
				return DirtyFlag.load();
			}

			size_t TakeQuietChangedFiles(
				ChangedFile* out, size_t capacity, double quietSeconds)
			{
				if (!out || capacity == 0)
					return 0;

				const double now = NowSeconds();
				size_t written = 0;

				std::lock_guard<std::mutex> guard(DirtyMutex);

				for (auto it = DirtyFiles.begin();
					it != DirtyFiles.end() && written < capacity; )
				{
					if (now - it->second < quietSeconds)
					{
						++it;
						continue;
					}

					// Quiet long enough: prove the writer has finished by taking
					// exclusive access. A failure means "still being written", so
					// the entry is left for a later frame instead of being fed to
					// the engine half-written.
					HANDLE probe = ::CreateFileW(
						it->first.c_str(),
						GENERIC_READ,
						0,                      // no sharing: an open editor blocks us
						nullptr,
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL,
						nullptr);

					if (probe == INVALID_HANDLE_VALUE)
					{
						const DWORD error = ::GetLastError();

						if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
						{
							// Deleted, or saved under a new name: drop it, there
							// is nothing left to reload.
							LogDebug(L"IniHotReload: '%ls' disappeared, dropping it.",
								it->first.c_str());
							it = DirtyFiles.erase(it);
							continue;
						}

						++it;   // still locked; try again next frame
						continue;
					}

					::CloseHandle(probe);

					// Map the full path back to the configured spelling, so the
					// engine is handed exactly what the user wrote.
					for (size_t i = 0; i < Count(); ++i)
					{
						if (Get(i).FullPath == it->first)
						{
							ChangedFile& slot = out[written];
							::strncpy_s(slot.Name, Get(i).Name, _TRUNCATE);
							slot.FullPath = it->first;
							++written;
							break;
						}
					}

					it = DirtyFiles.erase(it);
				}

				if (DirtyFiles.empty())
					DirtyFlag.store(false);

				return written;
			}
		}
	}
}
