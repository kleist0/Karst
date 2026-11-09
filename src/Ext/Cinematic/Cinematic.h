#pragma once

// Cinematic - keeps the mouse usable while a mission/cinematic locks input.
//
// ---------------------------------------------------------------------------
// WHAT THE ENGINE DOES (measured in gamemd 1.001)
// ---------------------------------------------------------------------------
//
// The `LockInput` trigger action runs the code at 0x68410D. Its first-time path
// is:
//
//     0x684113  mov  al,[Scenario+0x35A2]     ; already locked?
//     0x684120  jne  ...                      ; yes -> only bump the counter
//     0x684127  call DisplayClass::something   ; 0x6938C0
//     0x684134  call [WWMouse->vtable+0x28]   ; some "cursor is visible" test
//     0x684143  call [WWMouse->vtable+0x0C]   ; HideCursor
//     0x68414C  mov  byte [Scenario+0x35A2],1 ; the cinematic flag
//     0x684159  mov  byte [0xA8ED9C],1        ; "input is locked"
//     0x684160  call InputManager::DoSomething (0x54F720)
//
// and `UnlockInput` decrements the reference count at Scenario+0x62C and, when it
// reaches zero, undoes all of that.
//
// `0xA8ED9C` is what the input handlers consult: with it set, the paths at
// 0x54F2xx and 0x54F8xx skip their normal work, which is the "the mouse does
// nothing" part. The cursor disappearing is the HideCursor call.
//
// ---------------------------------------------------------------------------
// WHAT THIS MODULE DOES, AND WHAT IT DELIBERATELY LEAVES ALONE
// ---------------------------------------------------------------------------
//
// While `Scenario+0x35A2` is set it clears `0xA8ED9C` again and re-shows the
// cursor once per lock, so the pointer keeps working during the cinematic.
//
// It does NOT touch `Scenario+0x35A2` itself and does NOT touch the lock counter
// at Scenario+0x62C, so letterboxing, mission scripting and the matching
// UnlockInput all behave exactly as before - only the two input-side effects are
// dropped.
//
// ---------------------------------------------------------------------------
// CONFIGURATION  (karst.ini)
// ---------------------------------------------------------------------------
//
//     [Cinematic]
//     KeepMouse=yes      ; no = the engine's own behaviour (mouse disabled)
//
// No hooks: this is two byte writes and, on the first frame of each lock, one
// engine call, all from the existing main-loop hook.

namespace Karst
{
	namespace Ext
	{
		namespace Cinematic
		{
			// Reads [Cinematic] from karst.ini. Called once from OnGameStart().
			void Init();

			// Called every frame from the main loop.
			void OnFrame();
		}
	}
}
