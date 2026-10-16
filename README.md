# Karst

《红色警戒2：尤里的复仇》引擎扩展 DLL，基于 [Syringe](https://github.com/Ares-Developers/Syringe) 注入，
使用 YRpp 头文件描述游戏内部结构。

> **接手这个项目？先读 [`docs\二次开发须知.md`](docs/二次开发须知.md)。**
> 那一份讲：怎么接收、怎么清理进 git、怎么从源码编出成品、**该往游戏目录放哪些文件**、
> 硬编码路径清单、以及已知债务。本文只介绍项目自身与它的功能。

Karst 由**两半**组成，缺一不可：

* **C++ 侧** —— `Release\Karst.dll`：hook 引擎、INI 热重载、按国家外观。
* **C# 侧** —— `karstpatcher\`（脚本宿主 `KarstPatcher.dll` + 引擎绑定 + Roslyn）：
  在游戏进程里编译并加载用户写的 `.cs` 脚本。

只复制 `Karst.dll` 会得到一个**没有脚本系统**的 Karst，而且是静默失效。
完整清单见交接文档 §4。

在 `src\Ext\` 下添加自己的 C++ 功能，在 `tools\script-studio\` 里添加脚本积木。

## 目录结构

```
Karst\
├─ Karst.sln              解决方案（Debug|x86 / Release|x86）
├─ Karst.vcxproj          Visual Studio 工程
├─ Karst.rc / resource.h  版本资源（公司名、版本号等）
├─ karst.ini              配置模板，和 DLL 一起放到游戏根目录
├─ README.md              本文件
├─ scripts\               构建后脚本预留目录
├─ YRpp\                  游戏内部结构头文件（本工程自带一份副本）
└─ src\
   ├─ dllmain.cpp         DllMain + 所有 DEFINE_HOOK 钩子
   ├─ Karst.h/.cpp        主对象：生命周期、配置装配、每帧回调
   ├─ Version.h           版本号集中定义
   ├─ Config\
   │  ├─ INIConfigReader.h/.cpp   通用 INI 解析器
   │  └─ KarstConfig.h/.cpp       karst.ini 的类型化视图
   ├─ Core\
   │  └─ Log.h/.cpp       日志（调试输出 + 可选 karst.log）
   └─ Ext\
      └─ README.md        功能模块规范，新功能放这里
```

## 快速入口（双击即可）

日常只有两个入口：

| 文件 | 作用 |
| --- | --- |
| `package.bat` | **打包**：构建两半（C++ + C#）+ 校验 hook，在 `发布包\` 下产出一个**时间戳文件夹**（如 `2026-10-12-10-08-01\`），里面就是可直接拖进游戏目录的全套文件。默认**不打 zip**（加 `-Zip` 才打） |
| `tools\script-studio\打开脚本工作室.bat` | 打开积木式脚本工作室（独立浏览器窗口） |

**`package.bat` 不碰任何游戏目录** —— 打包与安装是两件事，装的人自己决定游戏在哪。
那个文件夹里带着 `README-安装.txt`，照着放即可。

需要更细粒度动作时（不属于日常流程）：

| 想做的事 | 用什么 |
| --- | --- |
| 只构建 C# 那半（不打包、不编 C++） | `refs\karstpatcher\build_refs.bat Release` |
| 构建 + 直接部署到**本机**测试游戏目录 | `tools\deploy_to_test_game.bat`（⚠️ 写死了本机路径，**别给玩家**） |
| 打包时顺带出一个 zip | `package.bat -Zip` |
| 只打包不重新构建 | `package.bat -NoBuild` |

前两个是**转发脚本**：真正的工作在 `refs\karstpatcher\build_refs.bat` 与
`tools\deploy_to_test_game.bat`，它们用 `%~dp0` 定位自己的依赖，**不能被复制到别处**
（复制后会在新位置找 `.csproj` / `package_full.ps1`，然后失败）。

`deploy_to_test_game.bat` 的默认游戏目录写在 `tools\deploy_to_test_game.bat` 顶部一行，
也可以传参：`deploy_to_test_game.bat "D:\别的游戏"`。

> ⚠️ 部署会**覆盖**游戏目录里的 `karst.ini`（含 `DebugLog=no`）。如果你手工把它改成了
> `yes` 来排错，重新部署后要再改一次。另外游戏在运行时该脚本会拒绝部署（DLL 被锁）。

## 编译

需要 Visual Studio 2019/2022/2026，安装「使用 C++ 的桌面开发」工作负载
（x86 工具集 + Windows 10 SDK）。

命令行：

```powershell
msbuild Karst.sln /p:Configuration=Release /p:Platform=x86
```

产物：`Release\Karst.dll`

工程默认使用 `v142` 工具集（和目录里另外三个项目保持一致）。若本机没装 v142，
在 `Karst.vcxproj` 里把两处 `<PlatformToolset>v142</PlatformToolset>` 改成 `v143` 即可。

### 关于自带的 YRpp

`YRpp\` 是从本工作区 `October Revolution` 项目复制的一份副本，**不是** git 子模块。
上一台机器上的 VS 版本较旧，这份头文件在当前 VS 下需要三处机械修改才能编译，
改动清单见 [`YRpp\PATCHES.md`](YRpp/PATCHES.md)。其中最需要注意的是
`DEFINE_HOOK` 的地址**不能写 `0x` 前缀**（这个副本的 `declhook` 宏会自己补 `0x`）：
写 `7CD810`，不要写 `0x7CD810`。

如果你换成更新版的 YRpp / Syringe.h，请重新核对 `src\dllmain.cpp` 里每个钩子。

## 已实现的功能

### ShowByCountries / ShowBySHP —— 按国家显示不同建筑图像

在建筑的 ART 段里配置两个平行列表，按**拥有者的国家**切换显示图像：

```ini
[GACNST]
ShowByCountries=Americans,Alliance,French,Germans,British
ShowBySHP=GAPILE,GATECH,GAPOWR,GAREFN,GADEPT
```

美国玩家（或 AI）看到的建造厂变成 GAPILE，法国玩家看到 GAPOWR，依此类推。
国家不在列表里 → 完全不生效。

- 完整说明、键名写法、校验规则与排查清单：[docs/已有功能0.1.0.md](docs/已有功能0.1.0.md)（第 1 节）
- 配置有误时（数量不一致/国家名不存在等）**进游戏前**弹错误框并写日志，功能整体不生效
- 实现位置：`src\Ext\ShowByCountry\`，钩子登记在 `src\dllmain.cpp`

### IniHotReload —— 运行中重读规则 INI

游戏运行期间保存**你列出的**规则 ini，Karst 立刻重读并把数据灌回引擎的类型对象，
不用退出战斗：

```ini
; 游戏目录下 karst.hotreload.ini
[IniHotReload]
Enabled=yes
Files=rulesmd.ini,artmd.ini,aimd.ini
```

列表就是**全部边界**：没列出的文件（`RA2MD.INI`、`karst.ini`、`ddraw.ini` …）完全不碰；
`.ini` 后缀写死在程序里。它**不新增任何钩子** —— 重载搭在已有的主循环钩子上执行，
所以 hook 数仍是 11。

- 完整说明、限制与排查清单：[docs/已有功能0.1.0.md](docs/已有功能0.1.0.md) 第 1 节
- 用法、边界与排查清单：[docs/已有功能0.1.0.md](docs/已有功能0.1.0.md)（第 1 节）
- 实现位置：`src\Ext\IniHotReload\`
- 不自带验证工具（不开游戏）：`tools\check_targets.cpp`（列表与边界）、
  `tools\check_watch.cpp`（监视线程行为）

## 使用

> 完整的部署清单（每个文件、是否必需、以及项目里没有的那两个 config）见
> [`docs\二次开发须知.md`](docs/二次开发须知.md) §4。这里只给最小步骤。

1. 复制 `Release\Karst.dll` 到 `gamemd.exe` 所在目录。
2. 按需复制 `karst.ini`、`karst.hotreload.ini`、`karst.scripts.ini` 到同一目录。
3. **想要 C# 脚本功能，还必须把整个 `karstpatcher\` 目录复制到同一目录**
   （内容 = `refs\karstpatcher\Extension\bin\Release\*.dll` 加两个 `*.config.json`）。
   少了它，用户写的 `.cs` 会被**静默忽略**。
   用 `powershell -ExecutionPolicy Bypass -File tools\package_full.ps1 -Deploy "<游戏目录>"`
   可以一步做完 1–3。
4. 用 `Syringe.exe "gamemd.exe"` 启动游戏（或直接用 Ares 的 `RunAres.bat`）。
5. **`gamemd.exe` 与 `Syringe.exe` 必须都以管理员身份运行**——实测未提权时
   Syringe 会直接报 `No or invalid command line arguments given` 并退出，
   这与参数写法无关（已在本机对照验证：连装好 Ares 的目录也一样失败）。

启动后 `karst.log`（需 `DebugLog=yes`）出现 `Karst started` 与
`First frame reached`，即表示注入成功、两个钩子都已生效。

## 配置

见 `karst.ini`，全部键都可省略：

| 键 | 取值 | 默认 | 说明 |
| --- | --- | --- | --- |
| `Enabled` | yes / no | yes | 总开关，no 时 DLL 加载但不做任何事 |
| `LogLevel` | off / error / warn / info / debug | info | 输出到调试器的详细程度 |
| `DebugLog` | yes / no | no | 额外写 `karst.log` 到游戏目录（UTF-8） |
| `ShowActiveMessage` | yes / no | yes | 预留；见下方说明，骨架暂不在消息栏显示内容 |

## 代码入口

- **`KarstExtension::OnProcessAttach`** —— 在 `DllMain` 里调用。此时持有加载器锁，
  只做赋值，不做文件/堆操作。
- **`KarstExtension::OnGameStart`** —— 由 `0x7CD810`（gamemd.exe 入口点）的钩子触发。
  读取 `karst.ini`、初始化日志、注册功能，**新功能都从这里挂进去**。
- **`KarstExtension::OnFrame`** —— 由 `0x55D360`（主循环）的钩子触发，每帧一次。
  骨架只用它在第一帧写一条日志；不需要每帧逻辑就把这个钩子删掉。

> 类名是 `KarstExtension` 而不是 `Karst`，因为配置和日志代码位于 `namespace Karst`，
> 同名类和命名空间在同一作用域下无法共存。

## ⚠ 触碰游戏内存前必读

这是本项目开发中实际踩到的坑，请务必先理解：

YRpp 用两种宏绑定引擎全局，行为完全不同（见 `src\Core\MemorySafety.h` 的完整说明）：

- `ALIAS(HouseClass*, Player, 0xA83D4C)` —— 是**绑定到固定地址的引用**。
  读它就等于直接读 `0xA83D4C`。所以 `if (HouseClass::Player)` 在游戏外**必崩**。
- `ALIAS_O(MessageListClass* const, Instance, 0xA8BC60)` —— 是 **DLL 内的变量**，
  它的值才是那个地址。读它安全，但它给出的指针仍需校验。

更关键的是：**用 `VirtualQuery` 判定「地址可读」并不等于「这是一个有效的引擎对象」**。
在本机实测中，`0xA8BC60` 这个固定地址在普通 32 位进程里恰好落在已提交的映射内存上，
于是校验通过、指针存活，一调用 `PrintMessage` 立刻 `0xC0000005` 崩溃。

因此 `Karst::MemorySafety` 只能过滤掉明显的野读，**不能把过期指针变安全**。
正确做法是等引擎自己给出「已初始化」的信号（例如 `HouseClass::Array` 里出现了玩家对象，
或某个 Scenario 标志就绪），而不是只做一个可读性检查就去调引擎。

基于这个结论，`OnFrame` 目前**完全不触碰任何引擎对象**，只写日志。
`OnFrame` 的注释里留了显示启动提示的正确写法与前置条件，需要时再启用。

## 验证（不需要启动游戏）

`tools\smoke_test.cpp` 可以在不启动游戏的前提下验证 DLL 的加载与钩子链路：

```powershell
# 与 DLL 同为 32 位
cl /nologo /EHsc /Fe:smoke_test.exe tools\smoke_test.cpp
# 在放着 Karst.dll 与 karst.ini 的目录里运行
smoke_test.exe
```

它会依次执行 LoadLibrary、调用两个导出钩子、FreeLibrary，
每一步都用 SEH 保护并立即刷新输出。全部通过会打印 `SMOKE TEST PASSED`，
同时 `karst.log` 里应出现 `Karst started` 和 `First frame reached`。

这一工具能验证 DllMain、配置读取、日志、两个导出入口和卸载路径；
**不能**验证 Syringe 的钩子注册，也不能验证任何解引用真实游戏结构的代码。

## 添加一个新功能

看 `src\Ext\README.md`，核心就三步：

1. 在 `src\Ext\` 新建 `MyFeature.h/.cpp`；
2. 在 `KarstExtension::OnGameStart()` 里调 `Ext::MyFeature::Init();`；
3. 把文件加入 `Karst.vcxproj`（在 VS 里添加会自动完成）。

## 注意

- 只针对 YR 1.001 的 `gamemd.exe`（校验和见 Syringe）。地址写错会直接崩游戏。
- 所有钩子集中在 `src\dllmain.cpp`，新增钩子请在那里登记，保持地址清单可读。
- EA 未认可也不支持本项目。仅供学习与 MOD 开发使用。
