# refs\DynamicPatcher —— 给脚本用的引擎接口层（外部参考项目）

> **注意程序集已改名**：为避免与参考项目**同时安装时重名冲突**，产物不叫原来的名字。
> **命名空间与类名保持原样**，所以参考项目里的脚本示例仍可直接编译。

| 程序集 | 原名 | 命名空间（未改） |
| --- | --- | --- |
| `KarstPatcher.dll` | `DynamicPatcher.dll` | `DynamicPatcher` |
| `KarstYRpp.dll` | `PatcherYRpp.dll` | `PatcherYRpp` |
| `KarstExtension.dll` | `Extension.dll` | `Extension` |

改动清单（都改到位，缺一个就运行不起来）：

* 三个 `<AssemblyName>`
* `CompilationManager.cs` 里给用户脚本编译用的引用列表 `this.references.Add("KarstPatcher.dll")`
* `PatcherYRpp.csproj` / `Extension.csproj` 的 `<Reference Include=...>` 与 `HintPath`
* `Program.cs` 的工作目录 `BaseDirectory\KarstPatcher` 与 COM `ProgId("KarstPatcher")`
* `Logo.cs` 的 `Path.Combine("KarstPatcher", "logo")`
* 三个 `Properties\AssemblyInfo.cs` 的 `AssemblyTitle` / `AssemblyProduct`

这个目录**不是 Karst 自己写的代码**，是为了让用户能用脚本（尤其是 C#）驱动引擎，
从开源参考项目 YRDynamicPatcher 取来的三层源码。

## 各层是什么、来自哪里

| 目录 | 来源 | 规模 | 作用 |
| --- | --- | --- | --- |
| `PatcherYRpp\` | https://github.com/Xkein/PatcherYRpp （`main`） | 83 个 .cs / 8279 行 | **绑定层**：`Pointer<T>`、`AbstractClass`、`BuildingClass`、`BulletClass`、`MapClass`、`LaserDrawClass`… 用 C# 直接操作引擎对象 |
| `Extension\` | https://github.com/Xkein/PatcherExtension （`main`） | 31 个 .cs / 2836 行 | **扩展层**：`Ext/`（`TechnoExt`/`BulletExt`）、`Script/`（`TechnoScriptable`/`BulletScriptable`）、`Decorators/`、`Components/` |
| `DynamicPatcher\` | https://github.com/Xkein/YRDynamicPatcher （ver1.0 本地副本） | 15 个 .cs | **运行时**：hook 管理、Roslyn 编译、脚本加载、日志 |

### 为什么参考项目的本地副本不够用

`YRDynamicPatcher-ver1.0` 与 `-ver2.1_alpha` 里，这三个关键部分**都是空的 git 子模块**
（`.gitmodules` 指向上面两个仓库，`Sample\DynamicPatcher\Projects\{YRpp,Extension}` 是空目录）。
换句话说：**那份参考项目缺的正是"给脚本用的接口"**，所以上面两层的源码是从 GitHub 单独取的。

## 构建目标与依赖

三者都是 **.NET Framework 4.8**、`LangVersion=latest`、`AllowUnsafeBlocks=true`。

- `PatcherYRpp` 依赖：`DynamicPatcher.dll`、`System.Memory`、
  以及 `Microsoft.VisualStudio.Interop` / `Microsoft.VisualStudio.OLE.Interop`
  （只被 `Interfaces.cs`、`LocomotionClass.cs` 以 `using Microsoft.VisualStudio.OLE.Interop;` 引用，
  来源是 VS SDK，可能需要替换或去掉——**待编译时确认**）
- `Extension` 依赖：仅 `DynamicPatcher.dll`
- `DynamicPatcher`（宿主）依赖 NuGet：`Microsoft.CodeAnalysis.CSharp.Workspaces`(Roslyn)、
  `Newtonsoft.Json`、`System.Buffers`、`System.Memory`、`System.Numerics.Vectors`、
  `System.Runtime.CompilerServices.Unsafe`、`System.Threading.Tasks.Dataflow`

构建需要 **.NET Framework 4.8 参考程序集/目标包**。两条路：

1. VS Installer 勾选 ".NET Framework 4.8 SDK" + "目标包"
2. 或者给项目加 NuGet 包 `Microsoft.NETFramework.ReferenceAssemblies`
   ——**推荐**：随项目走，别人 clone 下来不用改 VS 安装就能编译

## 分发策略（重要）

**最终分发给玩家的是编译好的 DLL，玩家机器零额外依赖。**

- .NET Framework 4.8 **运行时**：Win10 1903+ / Win11 **系统自带**，玩家不用装
- 开发包（SDK/目标包）只在**编译时**需要，不进游戏、不分发
- 所以**不在用户机器上做运行时编译**（参考项目把 Roslyn 塞进游戏，首次编译卡顿 + 一堆依赖）；
  我们改在开发机上把脚本编译成 DLL

## 构建

```
build_refs.bat            （Release，默认）
build_refs.bat Debug
```

从干净状态（删掉 bin/obj）实测一次通过，三个产物：

| 产物 | 大小 |
| --- | --- |
| `DynamicPatcher\bin\Release\DynamicPatcher.dll` | 59,904 |
| `PatcherYRpp\bin\Release\PatcherYRpp.dll` | 165,376 |
| `Extension\bin\Release\Extension.dll` | 49,152 |

**前置要求**（开发机已满足）：

1. **.NET Framework 4.8 SDK + 目标包**（VS Installer → 单个组件）
2. 首次构建需要联网，让 NuGet 还原 Roslyn / Newtonsoft

> ⚠️ 如果没有目标包，会报 `MSB3644`。**不要**用 NuGet 的
> `Microsoft.NETFramework.ReferenceAssemblies` 去绕——实测只能把错误从 73 降到 6，
> 然后卡在一个**隐式 `System.Core 3.5`**（来自机器上残留的 v3.5 开发包）上，报
> CS0433/CS7069。正确做法是装上官方目标包。

## 为了让源码构建通过所做的适配（都有原因，别随手改回）

参考项目的源码按**它自己的解决方案结构**编写，与这里的目录结构不同，因此做了这些必要修改：

| 改动 | 原因 |
| --- | --- |
| `PatcherYRpp.csproj` / `Extension.csproj` 的 `HintPath` 减一层 `..\` | 原始结构里项目深两层（`Sample\DynamicPatcher\Projects\`），这里平铺 |
| `Extension.csproj`：删除 `ProjectReference`，改为直接引用两个 DLL | 原引用指向 `..\YRpp\`、`..\PatcherYRpp.Utilities\`，**那是它自己解决方案里的路径，这里不存在** |
| `PatcherYRpp.csproj`：Release 配置补 `AllowUnsafeBlocks` | 原文件只在 Debug 配置里有，Release 编译直接 395 个 CS0227 |
| `DynamicPatcher.csproj`：`RegisterForComInterop` 改 false | 原为 true，构建时报 `MSB3216 无法注册程序集 - 拒绝访问`（需要管理员）；脚本宿主不需要 COM 注册 |
| 删除 `IDecorative.cs` / `DecoratorComponent.cs` 的 `using PatcherYRpp.Utilities;`→ **仅 IDecorative 删了** | 见下 |
| **新增 `PatcherYRpp\Helpers\MathEx.cs`** | `MathEx` 属于那个**未随仓库提供**的 `PatcherYRpp.Utilities` 项目，全互联网上这两个仓库都没有它；Extension 只用了一处 `MathEx.Random.Next()`，因此补了最小实现 |
| `Libraries\` 目录 | `PatcherYRpp.csproj` 引用 `..\Libraries\*.dll`；`System.Memory` / `System.Runtime.CompilerServices.Unsafe` / `WindowsBase` 从 NuGet 缓存取，两个 `Microsoft.VisualStudio.*.Interop` 从 VS 安装目录取 |

**注意**：`Extension\Utilities\` 是 Extension 项目**自己的子目录**（namespace 是 `Extension.Utilities`），
不是那个缺失的 `PatcherYRpp.Utilities` 项目——别混。

## 运行期必需的修改（不改就跑不起来）

这些不影响编译，但缺了脚本系统只会"空转"。

| 修改 | 原因 |
| --- | --- |
| `DynamicPatcher.csproj` 的 Release 加 `DEVMODE` | **它是编译器开关，不是"开发模式"**：`CompilationManager.CompileFile` 里"解析源码 → Roslyn Emit → 打包"整段都包在 `#if DEVMODE` 内。Release 不定义它 ⇒ **根本不编译脚本**，只会去 `Assembly.LoadFrom` 一个永远不存在的文件。实测现象：日志有 `compiling: X.cs`，然后直接 `FileNotFoundException: Build\Scripts\X.dll`，而 `Build\Scripts` 是空的。 |
| `CompilationManager`：`Path.ChangeExtension(path, "tmp")` → `"dll"` | 参考项目用 `.tmp` 当中间产物，靠 pack 环节转成正式产物。没有 pack 时它就是最终程序集，而 `Assembly.LoadFrom` 拒绝 `.tmp`（`BadImageFormatException: 该模块应包含一个程序集清单`）。 |
| `CompilationManager`：`if (solutionlist.Count >= 0)` → `> 0` | **上游 bug**：`>= 0` 恒为真；工作目录里没有 `.sln` 时 `solutionlist[0]` 直接 `ArgumentOutOfRangeException`。修好后日志出现 `solution not found`（正常的跳过分支）。 |
| `CompilationManager`：读 `preprocessor_symbols`（配置里写的是 `#preprocessor_symbols`） | 键名不一致 ⇒ 符号定义被**静默忽略**。已把默认配置的键名统一为代码读的那个。 |
| `PackageManager.UnPack`：没有 `.pkg` 时直接返回 | 同一处 `#if DEVMODE`：`Pack` 只存在于 DEVMODE 构建，普通构建只有 `UnPack`，而它要读的 `.pkg` 从未生成過 ⇒ 每个脚本都 `FileNotFoundException`。现在没有包就用刚编译好的程序集。 |
| 配置文件名 `dynamicpatcher.config.json` → `karstpatcher.config.json`；`compiler.config.json` 改由宿主自动生成 | 与程序集改名保持一致。`Patcher.Init` 读不到配置会直接抛异常（`Program::Activate failed 0x80131604`），而这两个文件的内容是固定的，让用户手放没有意义——由 `ClrHost.cpp` 在加载前生成。 |

### 关键机制：脚本是怎么被"应用"的（以及为什么现在还是空转）

`Patcher.ApplyAssembly()` 遍历脚本程序集里每个类型的每个成员，找带 `[Hook]` 属性的，交给 `HookManager.ApplyHook`。

**但是**：`Extension` 层里 **38 处 `[Hook(...)]` 声明全部是注释状态**；非注释的 `HookType.` 引用全是宿主自己的 switch 分支（类型映射），**没有一处是真正的 hook 声明**。

实测后果：

* 脚本**能**编译、加载，日志出现 `appling: <脚本程序集>`（`ApplyAssembly` 扫描到了脚本类，`in class Scripts.X:` 后面是空的 = 没找到 hook 属性）
* 但 `ExtMap`（`TechnoExt`/`BulletExt` 的扩展数据表）**不会被填充**——填充它的正是那些被注释掉的 hook（`TechnoClass::CTOR = 0x6F3260` 等）
* ⇒ 脚本**不会被真正执行**：`OnUpdate`/`OnFire` 缺少驱动来源（`ScriptManager` 要靠 `ExtMap` 拿到每个对象的 `TechnoExt` 才能绑定脚本）

**要让脚本动起来，必须按需启用 Extension 的 hook**，那一刻才真正进入地址冲突风险区（见文末）。

## 接入 Karst 的三步（见 HANDOFF）

1. ✅ 补环境（.NET Framework 4.8 SDK + 目标包）
2. ✅ 单独编译这三层，产出 DLL（`build_refs.bat`，删掉 bin/obj 后干净重建可重现）
3. 🔶 **进行中**：C++ 宿主已打通（`Karst\src\Script\ClrHost.h/.cpp`，在 `OnGameStart` 启动 CLR）。
   实测：`ScriptHost: the script system is loaded and active`；参考脚本 3/5 编译并加载成功；
   `appling: <脚本程序集>` 确认脚本类被扫描。
   **剩余**：启用 Extension 的 hook（当前 38 处全为注释），并逐一验证它们与
   Ares/Phobos/Kratos 的 3215 个 hook 不冲突。

**第 3 步不通过，就不整体接入。**

## 风险（必须验，不能假设）

- 绑定层全是**硬编码 VA**（如 `AbstractTypeClass.ArrayPointer = 0xA8E968`、
  `MapClass.DamageArea = 0x489280`、`LaserDrawClass = 0x54FE60`），
  这是标准 YR 1.001 的地址；本环境装了 Ares + Phobos + Kratos（3204 个 hook），
  **地址与结构偏移必须实机核对**。已有一个疑点：按 `0xA8E968` 读 `AbstractTypeClass::Array`
  实测得到的是垃圾值。
- 参考项目自己都没解决"两套 hook 引擎互相看不见"，脚本装的 hook 与
  Syringe/Ares/Phobos 的冲突必须我们自己检测。


---

## 实测发现：ver1.0 这套源码缺少 OnUpdate 驱动层（2026-10-12）

### 现象

脚本能编译、加载、`appling` 到脚本类，`TechnoTypeExt` 也能读到 `Scripts=`，
但脚本的 `OnUpdate` / `OnFire` **永远不会被调用**。

### 证据

搜索全仓库（宿主 `DynamicPatcher` + 扩展 `Extension`）的 `OnUpdate`：

| 位置 | 内容 |
| --- | --- |
| `Component.cs:283` | `public virtual void OnUpdate()` —— 定义 |
| `ExtComponent.cs:39` | `public override void OnUpdate()` —— 只负责启动未启动的子组件 |
| `Scriptable.cs:15` | `public void OnUpdate();` —— 接口声明 |
| 宿主层 | **0 个调用** |

`TechnoExt` 的 6 个 hook 全部是 `CTOR/DTOR/SaveLoad`，**没有 Update 类 hook**。
`ScriptManager.RefreshScriptComponents`（唯一会碰组件的地方）只在**脚本程序集刷新**时运行，
而且它只做附加，不做每帧驱动。

### 结论

驱动层不在我们拿到的源码里。它很可能位于 ver2.1 的空子模块
`DPExtension-Dionysus`（`Sample/DynamicPatcher`），或需要使用者自行实现。

**要补的东西（概念）**：

```csharp
[Hook(HookType.AresHook, Address = <TechnoClass::Update 的地址>, Size = 5)]
static UInt32 TechnoClass_Update(REGISTERS* R)
{
    foreach (TechnoExt 实例 in TechnoExt.ExtMap) { 实例.OnUpdate(); }
}
```

### 另外两个实测到的问题（补驱动层时要一并修）

| 问题 | 原因 | 现象 |
| --- | --- | --- |
| 附加脚本时 `Type` 为 NULL | `TechnoClass_CTOR` 在**构造最开始**触发，`TechnoClass::Type` 字段尚未赋值，而 `TechnoExt.OnAwake` 里的 `Type.Scripts` 立刻读它 | 日志 `KarstDiag: TechnoExt.OnAwake type=NULL Scripts=NULL` |
| **hook 自我卸载** | 上一条抛出 `NullReferenceException`，`HookInfo.TryCatchCallable`（配置 `try_catch_callable: true`）捕获后调用 `TransferStation.UnHook()` | 日志 `[Warning] TransferStation unhook to run on origin code`，此后**所有单位创建都不再触发**（实测只触发 1 次） |

**教训**：在 `CTOR` 这种"对象还没构造完"的时机里不要访问对象字段；
hook handler 里抛异常会让整条 hook 被卸载，症状是"只有第一次生效"。

## 现有的诊断探针（A 阶段定位用，完成后应清理）

为定位上面这些问题，在源码里临时加了 `KarstDiag:` 日志。位置：

| 文件 | 探针 | 作用 |
| --- | --- | --- |
| `Extension\Ext\TechnoTypeExt.cs` | 构造函数 | 证明 `TechnoTypeClass_CTOR` hook 运行、ExtMap 分配 |
| `Extension\Ext\TechnoTypeExt.cs` | `LoadFromINIFile` | 证明 INI 被读、`Scripts=` 是否解析出内容 |
| `Extension\Ext\TechnoExt.cs` | 构造函数 | 证明单位创建时 `TechnoClass_CTOR` 运行 |
| `Extension\Ext\TechnoExt.cs` | `OnAwake` lambda | 证明 `Awake` 触发、`Type`/`Scripts` 是否可用 |
| `Extension\Components\ExtComponent.cs` | `Awake()` | 证明组件被唤醒、订阅者数量 |
| `Extension\Script\ScriptManager.cs` | `CreateScriptableTo` | 证明脚本被附加到对象 |
| `KarstPatcher\Scripts\DISKScript.cs` | `OnUpdate`（前 5 次） | 证明每帧回调被执行 |

**这些探针是排查用的，功能验证通过后应移除**（尤其 `DISKScript.cs` 在游戏目录里，
不属于仓库）。

## KARST FIX 标记

对参考源码的修改中，属于"修上游问题"而不是"适配目录结构"的，都加了注释标记：

* `Extension\Ext\TechnoExt.cs` / `BulletExt.cs`：构造末尾 `_extComponent.EnsureAwaked()`
  （上游只在 `ExtComponent` 属性被访问时才唤醒，而新建单位没人访问它）。
  **注意**：这个修改本身暴露了 `Type=NULL` 问题，见上文——A 阶段要改成"延迟附加"。
* `DynamicPatcher\Patcher.cs`：`ApplyExtensionAssembly()`
  （上游只扫 `*.cs`，预编译的扩展 DLL 不会被应用 hook）。
* `DynamicPatcher\PackageManager.cs`：无 `.pkg` 时直接返回。
* `DynamicPatcher\CompilationManager.cs`：`.tmp`→`.dll`；`Count >= 0`→`> 0`。

---

## 驱动层已实现（2026-10-12）—— OnUpdate 实测被调用

上一节记录的缺口（`OnUpdate` 无调用者）**已经补上并验证**。

### 设计：复用 Karst 的每帧点，不新增 hook

Karst 本来就有 `KarstExtension::OnFrame`（主循环帧边界），所以驱动不需要新 hook——
这保持了与 Ares/Phobos/Kratos 的冲突面不变。

```
Karst.cpp OnFrame()
  → Karst::Script::TickScripts()            [C++]
      → Program.DoTick()                    [托管，静态方法，COM Invoke]
          → Program.Tick 事件
              → ScriptManager.TickExtensions()
                  → 遍历 TechnoExt/BulletExt 的 ExtMap
                      → component.EnsureAwaked()          （在此唤醒，见下）
                      → component.Foreach(c => c.OnUpdate())
```

### 四处改动（都在参考源码里，已标注 KARST ADDITION）

| 文件 | 改动 | 为什么必须 |
| --- | --- | --- |
| `Extension\Ext\Container.cs` | 加 `AllExtensions` 属性 | 上游只有 `Find`/`FindOrAllocate`/`Remove`，**没有任何遍历出口**，驱动无法拿到"所有活的扩展"。返回的是**副本**，因为回调里可能增删 |
| `DynamicPatcher\Program.cs` | 加 `public static event Action Tick` + `DoTick()` | **依赖方向**：`Extension` 引用 `DynamicPatcher`，所以 `DynamicPatcher` 不能直接写 `TechnoExt`。用事件反转依赖，`Extension` 在静态构造函数里订阅 |
| `Extension\Script\ScriptManager.cs` | 加 `TickExtensions()` / `DriveExtensions<T>()`，并在静态构造函数里 `Program.Tick += TickExtensions` | 具体驱动逻辑 |
| `Karst\src\Script\ClrHost.cpp` / `Karst.cpp` | `TickScripts()`（缓存 `Program.DoTick` 的 `MethodInfo`，每帧 `Invoke`）并在 `OnFrame` 调用 | C++ 侧桥接 |

### 关键点：唤醒时机从"构造期"移到"首次 Tick"

`Component.OnUpdate()` 是**空的虚方法**（不遍历子组件），而脚本对象是**子组件**，
所以必须显式 `Foreach(c => c.OnUpdate())`。

`EnsureAwaked()` 放在驱动里（而不是构造函数），因为：

* `TechnoClass_CTOR` 在**对象构造最开始**触发，此时 `TechnoClass::Type` 仍是 `NULL`
* 构造期唤醒 → `OnAwake` 读 `Type.Scripts` → `NullReferenceException`
* 该异常被 `try_catch_callable` 捕获 → `TransferStation.UnHook()` → **hook 自我卸载**
* 症状：只有第一个单位生效，之后全部静默失效

移到每帧（此时对象已构造完、`Type` 有效）后，两个问题一起消失。

### 实测结果

```
TechnoExt CTOR        40 次
ExtComponent.Awake    40 次
attach script 'Disk'   1 次
Disk.OnUpdate          5 次   （探针上限；实际每帧）
TransferStation unhook  无
hook exception          无
```

`KarstDiag` 探针在验证通过后**已全部移除**（全仓库 0 处）。若需重新定位问题，
按本文件"现有的诊断探针"一节重新添加即可。

### 尚未驱动的事件（下一步）

目前只驱动了 `OnUpdate`。以下接口在 `Extension\Script\Scriptable.cs` 里声明，
**同样没有调用者**，需要按同一模式各接一个驱动：

| 接口 | 方法 | 需要的事件源 |
| --- | --- | --- |
| `ITechnoScriptable` | `OnFire(target, weaponIndex)` | `TechnoClass::Fire` |
| `IObjectScriptable` | `OnPut(coord, faceDir)` | 放置/生成时 |
| `IObjectScriptable` | `OnRemove()` | 移除时（可借 `TechnoClass_DTOR`） |
| `IObjectScriptable` | `OnReceiveDamage(...)` | 受伤时 |
| `IBulletScriptable` | `OnDetonate(coords)` | 子弹引爆时 |

**注意**：`ONFIRE` 这类事件的驱动需要在引擎函数处取参数（目标、武器序号），
和 `OnUpdate` 的"无参遍历"不同——要么挂对应函数，要么在现有 hook 里顺带取。
这是下一步的主要工作量。

---

## 全部事件驱动已实现（2026-10-12）

`Scriptable.cs` 里声明的事件接口**现在都有驱动了**。8 个 hook 全部挂载成功、
无异常、游戏稳定。

| 事件 | 驱动方式 | 地址 | 链式情况 |
| --- | --- | --- | --- |
| `OnUpdate` | Karst `OnFrame` → `Program.DoTick()` | 无（复用 Karst 帧） | 不涉及 |
| `OnFire` | `TechnoClass::Fire_NotVirtual` | `0x6FDD50` size 6 | 叠在 **Kratos** 上 |
| `OnReceiveDamage` | `TechnoClass::ReceiveDamage` 入口 | `0x701900` size 6 | 叠在 **Phobos** 上 |
| `OnDetonate` | `BulletClass::Detonate` 入口 | `0x4690B0` size 5 | 入口空闲（Kratos 挂内部 `0x4690C1`） |
| `OnRemove` | 复用已挂的 `TechnoClass_DTOR` | `0x6F4500` | 原有 |
| `OnPut` | 首次 Tick 触发 | 无（`ObjectClass::Put` 是虚函数） | 不涉及 |

### 几个设计取舍

* **`OnPut` 不加 hook**：`ObjectClass::Put` 是虚函数（vtable 槽 54），没有固定地址；
  而它在语义上等于"对象出现在世界上"，第一次被驱动时正好满足。坐标从
  `OwnerObject.Ref.Base.Base.GetCoords()` 读，读失败就用默认值（半构造对象不能中断帧）。
  朝向没有取——该访问器在各绑定类里位置不一，而 `OnPut` 主要用坐标。
* **`OnRemove` 复用 `DTOR`**：不额外占地址。
* **size 刻意与被叠加的扩展一致**（Fire 用 6 匹配 Kratos，ReceiveDamage 用 6 匹配 Phobos），
  这样链式保存/恢复的字节范围对齐。
* **参数偏移逐个对照反汇编验证**。例：`ReceiveDamage` 里 `sub esp,0xb4 / push ebx`
  之后首次读取是 `[esp+0xbc]`，减掉 `0xb4+4` 正好是原 `[esp+4]`，与签名首参
  `pDamage` 吻合。

### 实测（挂载阶段）

```
4690B0 BulletClass_Detonate          5
6F3260 TechnoClass_CTOR              5
701900 TechnoClass_ReceiveDamage     6
6FDD50 TechnoClass_Fire              6
6F4500 TechnoClass_DTOR              5
711835 TechnoTypeClass_CTOR          5
711AE0 TechnoTypeClass_DTOR          5
716123 TechnoTypeClass_LoadFromINI   5

hook exception: 无
gamemd: RUNNING
```

`OnFire` + `OnUpdate` 的**端到端效果已实测**：给 `[MTNK]` 加 `Scripts=Disk`
后，灰熊坦克开火会出现样本脚本画的绿色激光。

### 新增的源码改动（同样标注 KARST ADDITION）

| 文件 | 改动 |
| --- | --- |
| `Extension\Ext\TechnoExt.cs` | `TechnoClass_Fire` / `TechnoClass_ReceiveDamage` hook；`DTOR` 派发 `OnRemove`；`OnPutFired` 标志 |
| `Extension\Ext\BulletExt.cs` | `BulletClass_Detonate` hook |
| `Extension\Script\ScriptManager.cs` | `OnFireScripts` / `OnReceiveDamageScripts` / `OnRemoveScripts` / `OnDetonateScripts` / `OnPutScripts` + 共用 `DispatchObjectEvent` |

---

## 踩坑：hook 的 `Size` 必须落在**指令边界**上（2026-10-12，真实崩溃）

### 症状

给灰熊坦克开火后游戏直接退出，`patcher.log` 里**没有任何运行时异常**
（说明没走到 C# 异常处理），只有：

```
syringe.log: Exception (Code: 0xC0000005 at 0x136E002E)!
             Run: Done with exit code C0000005
```

### 定位

`0x136E0000` 正是 `patcher.log` 里第一条 `AresHookTransferStation alloc:`
——也就是**第一个挂载的 hook（`BulletClass_Detonate`）自己的桩**。崩溃在桩内偏移
`0x2E`，即"执行保存下来的原指令"那一段。

### 原因

`0x4690B0` 的前几条指令：

```asm
0x4690B0  push ebp                  ; 1 字节
0x4690B1  mov  ebp, esp             ; 2 字节
0x4690B3  and  esp, 0xfffffff8      ; 3 字节（83 E4 F8；imm8 符号扩展，不是 4 字节！）
0x4690B6  sub  esp, 0x94
```

`Size = 5` 覆盖到 `and` 的**第 2 个字节**就停了。链式桩会把"被覆盖的 5 字节"
当作"原指令"重新执行，于是 `and` 被从中间开始执行 → 指令流错乱 → `C0000005`。

**正确值是 6**（`1 + 2 + 3`，正好到 `sub` 之前）。

### 怎么避免

**每次挂 hook 前，用 `tools\disrva.py <exe> <地址> <条数>` 看清指令边界**，
让 `Size` 恰好等于"若干条完整指令"的总长度。

本项目的 8 个 hook 都按此核对过：

| 地址 | 指令构成 | size |
| --- | --- | --- |
| `0x4690B0` | `push`(1)+`mov`(2)+`and`(3) | **6** |
| `0x6FDD50` | `push`(1)+`mov`(2)+`and`(3) | 6 |
| `0x701900` | `sub esp,0xb4` —— **6 字节**（32 位立即数） | 6 |
| `0x6F3260` | `mov`(2)+`pop`(1)×3 | 5 |
| `0x711835` | 同上 | 5 |
| `0x711AE0` | `push`(1)×3+`mov`(2) | 5 |
| `0x716123` | `mov al,1`(2)+`pop`(1)×3 | 5 |

**两个容易算错的地方**：

1. `and esp, 0xfffffff8` 是 `83 E4 F8`（**3** 字节），不是 4 —— imm8 会符号扩展。
2. `sub esp, 0xb4` 是 `81 EC B4 00 00 00`（**6** 字节）—— 立即数大于 127 时用 32 位形式。

**判断方法**：不要靠"看起来像几条指令"，一定看反汇编给出的**每条指令起始地址**，
取相邻两个起始地址之差，累加到想要的条数。

---

## 接口扩展：EventDecorator（2026-10-12）

### 补上了缺失的类

参考项目的样本用 `EventDecorator`（例如 `GGIScript` 里的 `MissileFall`），
但这个类**不在 PatcherExtension 仓库里**——所以那些样本编译必然失败
（`CS0246: EventDecorator could not be found`）。而现有的 `Decorator` 只是个数据容器
（`Description` / `ID` / `Decorative`），**没有任何行为方法**。

新增 `Extension\Decorators\EventDecorator.cs`：

```csharp
public abstract class EventDecorator : Decorator
{
    public virtual void OnUpdate() { }   // 每帧，与脚本同一时机
    public virtual void OnRemove() { }
}
```

只镜像**真正会被驱动**的事件（见下），避免出现"永远不会被调用"的方法。

### 装饰器也需要驱动

`DecoratorComponent` 继承 `Component`，但**没有覆写 `OnUpdate`**，而装饰器本身
不是 `Component`（它是 `Component` 持有的数据对象）。所以驱动层原来根本走不到它。

新增：

* `DecoratorComponent.DriveDecorators()` —— 遍历持有的装饰器，对 `EventDecorator`
  调用 `OnUpdate`（先快照，避免 handler 增删装饰器破坏遍历）
* `DecoratorComponent.NotifyDecoratorsRemoved()` —— 移除前通知
* `ScriptManager.DriveExtensions` 里按类型（`TechnoExt`/`BulletExt`）调用
  `DriveDecorators()`——装饰器只挂在这两种扩展上

### 定位与用途

装饰器是"给某个对象挂一段带逻辑的自定义状态"的方式，**不必往组件树里再加节点**。
适合：持续若干帧的效果、需要绑到某个单位身上的计时器/标记。

`INIReader` 不需要额外工作——`TechnoTypeExt` 读 `Scripts=` 用的就是它，脚本侧可以直接用
（`Ext` 侧已经通过 `LoadFromINIFile` 把 INI 接进来了）。

### 顺带清理：不兼容的样本

`GGIScript.cs` / `MTNKScript.cs` 使用的是这套源码里**不存在**的旧版 API
（`EventDecorator` 的旧构造、`TechnoExt.Get/CreateDecorator`、`MapClass.Coord2Cell`、
`CellSpreadEnumerator`）——`EventDecorator` 现在有了，但其余几个仍属旧版，重写成本高。

它们被移到 **`<游戏目录>\_scripts_incompatible\`**，即 `KarstPatcher` **之外**。
注意 `CodeWatcher` 是**递归**扫描的（`SearchOption.AllDirectories`），
放在 `KarstPatcher` 的子目录里**仍会被编译**——必须移出 `KarstPatcher`。

清理后：3 个脚本编译成功、0 失败，`BadImageFormatException` 噪音消失
（原先编译失败会留下 0 字节 DLL，加载时才报错）。

---

## 两个框架级修复：编译失败不留垃圾 + 用户脚本/生命周期不得拖垮 hook（2026-10-12）

这两件事都属于同一类：**框架不能被用户代码或对象生命周期拖垮**。

### 修复 1：编译失败不再留下 0 字节 DLL

**根因是两个缺陷叠加**（`CompilationManager.cs`）：

1. Roslyn 的 `Emit(outputPath)` 在**编译失败时也会创建/截断输出文件**，留下一个 0 字节 DLL。
   原来的代码只是 `LogError` + `return null`，**没有清理**。
2. 新鲜度判断用的是 `File.Exists(outputPath)`——**0 字节文件也算"存在"**，而且它刚被写入，
   时间戳比源码**新**，于是判定"已编译、用旧程序集"，接着 `Assembly.LoadFrom` 对空文件抛出
   `BadImageFormatException: 该模块应包含一个程序集清单`。

症状因此具有误导性：日志先出现 `packing ...dll into ...pkg`，然后一个**与真实原因无关**的加载异常。

**修法（四处，互相配合）**：

* 两个 `Emit` 失败分支都调用 `DeleteCompileOutput(outputPath)` / `DeleteCompileOutput(pdbPath)`
* 新增 `HasUsableAssembly(path)` —— **存在且长度 > 0** 才算已编译，
  替换了 3 处 `File.Exists(outputPath)`（两处新鲜度判断 + `GetProjectBuildTime`）
* 两处 `Assembly.LoadFrom` 前加检查，把 `BadImageFormatException` 换成人能读懂的
  `no assembly was produced for 'X' - it did not compile (see the errors above)`

`HasUsableAssembly` 同时带来**自愈**：即使用户升级前已经攒下一批 0 字节 DLL，
它们会被当作"未编译"而重新编译，不会再把运行卡死。

**实测**：

| 测试 | 结果 |
| --- | --- |
| 放一个故意写错的脚本 | `failed: 1`，`Build\Scripts\BrokenTest.dll` **不存在** |
| 把好脚本的 DLL 截成 0 字节、并把时间戳改成比源码新 5 分钟 | **重新编译**，`compile file succeed!`，最终 3,584 B，**无 BadImageFormatException** |

### 修复 2：析构期不唤醒组件 + 脚本异常不牵连 hook

**这是我本轮自己引入的回归**，日志给得很明确：

```
在 Extension.Ext.TechnoExt.<.ctor>b__14_0()        ← OnAwake 的一个 lambda
在 Extension.Components.ExtComponent`1.Awake()
在 Extension.Ext.TechnoExt.get_AttachedComponent()  ← 唤醒发生在 DTOR 里
在 Extension.Script.ScriptManager.OnRemoveScripts
在 Extension.Ext.TechnoExt.TechnoClass_DTOR
[Warning] TransferStation unhook to run on origin code
→ 随后 0xC0000005，游戏退出
```

`TechnoClass_DTOR` 里派发 `OnRemove` 时调了 `AttachedComponent`，而那个访问器**会唤醒组件**
（`GetAwaked()` → `EnsureAwaked()`）。于是 `OnAwake` 在**正在销毁的对象**上读引擎字段 →
抛异常 → `try_catch_callable` 捕获 → **`UnHook()`**。

而 DTOR 这个 hook 还负责从 `ExtMap` 移除条目，所以它被卸载不只是丢一次通知，而是**整个会话持续泄漏**。

**修法**：

* `Component` 暴露 `IsAwaked`；`IHaveComponent` 增加 `AttachedComponentIfAwaked`
  （**不唤醒**，未唤醒时返回 null）
* `DispatchObjectEvent` 改用 `AttachedComponentIfAwaked`——析构期没有"已唤醒的组件"就没有脚本要通知，
  这是正确语义，而不是权宜
* `TechnoClass_DTOR` 整段加 try/catch（通知与 `ExtMap.Remove` 各自独立保护）
* `Container.Remove` 原来用 `Items[key]`，延伸对象已不在时抛 `KeyNotFoundException`
  （日志里确实出现过），改为 `TryGetValue`
* **异常隔离**：新增 `SafeScriptCall`，所有脚本/装饰器回调逐个包裹。
  以前一个脚本抛异常会经 hook 包装器触发 `UnHook()`，**一个坏脚本废掉整个 hook**；
  脚本出错是用户的事，不该让框架付代价

**实测（部署后重启）**：

```
succeed: 2, failed: 1
b__14_0 异常: 0        OnRemoveScripts 栈: 0
hook 自卸载: 0         KeyNotFoundException: 0
BadImageFormatException: 0
gamemd: RUNNING
```

### 三条可以推广的纪律

1. **对象构造期和析构期都不要读它的引擎字段**。`CTOR` 时 `Type` 还没赋值，
   `DTOR` 时字段已经在失效——两次都真实踩到过。
2. **不要用会"唤醒"的访问器**去做生命周期末尾的事。访问器的副作用（`GetAwaked()` 里
   藏了一次 `EnsureAwaked()`）在正常路径上很顺手，在析构路径上就是地雷。
3. **回调用户代码必须逐个隔离异常**。因为这层之上的包装器会用"卸载 hook"来兜底，
   一次用户错误就被放大成整条链路失效。