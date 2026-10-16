#pragma once

#include <Windows.h>

#include <string>

// IniHotReload, part 2 of 3 - noticing that a watched file changed.
//
// A background thread waits on ReadDirectoryChangesW for the game directory
// (recursively) and records which of the WATCHED files changed and when. It
// does exactly that: it opens the directory, it reads change notifications, it
// touches a mutex-protected map of paths. It never calls into the engine, never
// loads a file into a game object, and never talks to the game thread except
// through that map.
//
// The thread does NOT decide when a reload happens. It records a timestamp per
// path; the game thread (see IniHotReload.cpp) pulls entries that have been
// quiet for a while. That split is deliberate:
//
//   * "quiet for N seconds" is a policy about the GAME's frame, so it belongs
//     on the game thread, where waiting costs nothing.
//   * the watcher thread only ever touches its own map, so it can never block
//     or race the engine.
//
// This is where this module deliberately parts ways with the reference
// implementation (the upstream DynamicLoadINI.cs). That one sleeps inside
// its change handler and then stops the game thread with a semaphore to do the
// reload from the watcher thread. Karst already runs code once per frame on the
// game thread, so no such handshake is needed - and its failure mode (a
// semaphore count that is left at 1 when a reload dies, which then throws
// inside the game's own thread) does not exist here.

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
		{
			// Creates the directory handle and starts the watcher thread.
			// `gameDirectory` must end with a backslash. Returns false (with a
			// reason in the log) when the directory cannot be watched; the
			// caller should then leave the feature off.
			bool StartWatchThread(const wchar_t* gameDirectory);

			// Signals the thread and waits briefly for it to leave. Safe to call
			// when the thread was never started. Called from
			// KarstExtension::OnProcessDetach().
			void StopWatchThread();

			// One changed file, waiting to be reloaded.
			struct ChangedFile
			{
				char         Name[260];      // as configured - what the engine is given
				std::wstring FullPath;       // lower case, for logging and matching
			};

			// Pulls out every watched file that changed and has since been quiet
			// for at least `quietSeconds`, and that can be opened exclusively
			// right now (i.e. the editor has finished writing it).
			//
			// Called on the game thread, once per frame. Returns the number of
			// entries written to `out` (at most `capacity`).
			size_t TakeQuietChangedFiles(
				ChangedFile* out, size_t capacity, double quietSeconds);

			// True when at least one watched file has ever been seen changing.
			// Lets the caller skip the whole check with a single atomic read.
			bool HasPendingChanges();
		}
	}
}
