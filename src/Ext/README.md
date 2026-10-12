# src\Ext - feature modules

Each feature lives in its own file (or small folder) under this directory and
follows the same minimal contract, so features can be added or deleted without
touching the rest of the project.

## The contract

```cpp
// src\Ext\MyFeature.h
#pragma once

namespace Ext
{
    namespace MyFeature
    {
        // Called once from Karst::OnGameStart().
        void Init();

        // Optional: called every frame from the main loop hook.
        // Only add this if the feature actually needs per-frame work.
        void OnFrame();
    }
}
```

Then register it in `src\Karst.cpp`:

```cpp
void KarstExtension::OnGameStart()
{
    ...
    Ext::MyFeature::Init();
}

void KarstExtension::OnFrame()
{
    ...
    Ext::MyFeature::OnFrame();
}
```

And add both files to `Karst.vcxproj` (Visual Studio does this automatically
when you add the file through Solution Explorer).

## Rules of thumb

- **Read your own config keys.** Use `Karst::GetConfig()` and read from your own
  INI section, e.g. `config.GetRaw("MyFeature", "SomeKey", "default")`. Do not
  add feature-specific fields to `Karst::Config`; that class is only for keys
  that affect the whole DLL.
- **Respect the master switch.** `Karst::GetConfig().Enabled()` is false when the
  user set `Enabled=no`; check it before doing anything visible.
- **Log through `Core/Log.h`.** Use `LogInfo` / `LogWarn` / `LogError` /
  `LogDebug`; never write to disk yourself.
- **Hooks belong in one place.** Put `DEFINE_HOOK` blocks in the feature's own
  `.cpp` file, but keep the address list in `src\dllmain.cpp` up to date as a
  comment so the project has a single readable inventory of patched addresses.
- **Be defensive about engine state.** YRpp binds engine globals to fixed
  addresses, and some of them are references bound directly to those addresses.
  Read them through `Core/MemorySafety.h`, never directly - and remember that a
  readable address is not proof of a live engine object. Read the warning
  section in the project `README.md` before touching engine memory; it documents
  a crash this skeleton actually hit.
- **Never block the main thread.** No sleeps, no synchronous downloads, no modal
  dialogs on the game loop.
- **Do not call into the engine just because a pointer is non-null.** Wait for a
  signal the engine itself sets (a scenario flag, the player appearing in
  `HouseClass::Array`, and so on).
