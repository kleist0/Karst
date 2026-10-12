# scripts

Reserved for post-build steps, mirroring the layout used by the Kratos and
Phobos projects in this workspace.

The project currently builds and links with no post-build step, so nothing is
called from `Karst.vcxproj`. To add one, put a `.bat` or `.cmd` file here and
reference it from the `<PostBuildEvent>` element of the matching
`ItemDefinitionGroup`:

```xml
<PostBuildEvent>
  <Command>call "$(SolutionDir)scripts\copy_release.cmd"</Command>
</PostBuildEvent>
```

A typical `copy_release.cmd` copies `$(TargetPath)` into the game folder:

```bat
@echo off
copy /y "%~dp0..\Release\Karst.dll" "D:\Games\YR\Karst.dll"
```

Keep the game path out of version control if it is machine-specific; read it
from an environment variable instead.
