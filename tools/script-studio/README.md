# Karst 脚本工作室（Karst Script Studio）

> 用**搭积木**的方式写 Karst 的 C# 脚本：点事件、点条件、点动作，右边实时出现可编译的
> `.cs`；生成的脚本能列出来、再次编辑、删除、停用。
> **纯网页，不需要任何服务**，双击一个 HTML 文件就能用。

---

## 1. 给用户看的：怎么用

### 1.1 打开

双击：

```
Karst\tools\script-studio\KarstScriptStudio.html
```

> 🔴 **必须用独立浏览器窗口打开**（把文件拖进 Edge / Chrome 窗口，或双击）。
> 在 IDE / 编辑器的**内嵌预览面板**里打开会被多套一层沙箱：目录选择器、下载、剪贴板
> 全部被拒。这一点是实测的，这一点当时用 fs-probe.html 实测过。页面认得出这种情况并会直接告诉你
> "请用独立浏览器打开"，不会只丢一个通用报错。

### 1.2 搭一个脚本

1. 右上角 **选择脚本目录…** → 选 `<游戏目录>\KarstPatcher\Scripts`
   （只需一次；浏览器重启后要再点一次"重新授权"，页面会提示）
2. 左边 **事件** 里点一个，比如「每帧 OnUpdate」
3. 再点 **条件**（比如「每 60 帧」）和 **动作**（比如「记录自己的坐标/血量」）
   —— 积木只加进**当前选中**的事件卡，点卡片标题可以切换
4. 右边实时显示生成的 C#；底部会告诉你有没有问题
5. 点 **保存到脚本目录**，文件就写进 `Scripts\`

保存后**不用重启游戏**：Karst 的 `CodeWatcher` 会自动重编译。
去 `<游戏目录>\KarstPatcher\patcher.log` 看 `compile file succeed!`。

### 1.3 让脚本真的生效

生成的代码只是"脚本本体"，还要告诉游戏哪个单位用它 —— 在 `rulesmd.ini` 里：

```ini
[MTNK]
Scripts=StudioDemoLog      ; ← 类名，大小写敏感，不是文件名
```

页面右下角的 **绑定行** 会按你填的"绑定到哪个单位"生成这两行，直接复制即可。
`rulesmd.ini` 的改动**要重开一局**才生效（改 `.cs` 不用）。

> 页面**不会**替你改 `rulesmd.ini`。那是一个 74 万字节、被 INI 热重载监视着的关键文件，
> 替用户写它的风险远大于收益；页面只生成那一行给你复制。

### 1.4 管理已有脚本

中间下方「这个目录里的脚本」列出目录里的 `.cs`：

| 按钮 | 作用 |
| --- | --- |
| 打开 | 载入脚本。**如果是工作室生成的，会还原成积木**（见 §3.2）；否则进代码模式 |
| 停用 / 启用 | 在 `Foo.cs` ↔ `Foo.cs.pro` 之间切换。这是本项目推荐的停用方式（改扩展名后 Karst 不再编译它） |
| 删除 | 真删（有确认框） |

---

## 2. 给维护者看的：项目结构

```
Karst\tools\script-studio\
    studio.template.html      界面外壳（HTML/CSS/UI 接线）
    build_studio.ps1          构建：模板 + 积木库 + 游戏数据 -> 单文件 HTML
    verify_compile.ps1        离线编译验证器（对齐游戏内 Roslyn 参数）
    KarstScriptStudio.html    ← 构建产物，双击即用（唯一要交付给用户的东西）

    blocks\                   ★ 积木库与代码生成器（唯一真源）
        registry.js           5 个事件 / 9 个条件 / 11 个动作的定义与代码模板
        metadata.js           积木数据在 .cs 里的编码与解析
        codegen.js            doc -> C#（页面和测试共用同一份）
        validate.js           保存前的检查（错误 / 提醒）
    data\
        gamedata.json         构建时从 rulesmd/artmd 抽取的游戏数据（供查阅）
    demo\                     3 个演示脚本（由 make_demo.js 生成，非手写）
    tests\
        run_tests.js          积木库测试：逐块生成 + 编译 + 往返 + 校验
        check_page.js         产物静态检查（语法、DOM id、数据）
        make_demo.js          生成 demo\*.cs，可 --deploy 到游戏目录
        cases\                测试夹具（自动生成，可删）
```

### 2.1 关键设计：积木库是"可在 Node 里跑的纯 JS"

`blocks\*.js` 用 UMD 包装，**页面和 Node 测试使用同一份文件**：

```
         blocks\registry.js + codegen.js
                ├──────────────► 构建时内联进 HTML（用户点一下就看代码）
                └──────────────► node tests\run_tests.js --compile（机器验证）
```

这不是为了好看。它让"页面说 OK 就一定能编译"成为**可机器验证的断言**：测试用**页面调用
的同一个生成器**为每个积木产出 `.cs`，再交给与游戏内同参数的编译器。若两边各写一份实现，
页面完全可能显示绿色而测试编译的是别的东西。

### 2.2 关键设计：积木数据写在 .cs 文件里

生成的脚本第 6 行长这样（**注释**，不影响编译）：

```csharp
// @karst-studio {"v":1,"cls":"StudioDemoLog","base":"techno","binding":"MTNK","events":[...]}
```

**必须在文件里**：页面只能看到用户选中/拖进来的文件，没有任何目录推断能力，
"这个脚本当初勾了哪些事件"无法从生成后的代码反推。没有这一行，"再次编辑"就退化成
"用记事本改一大段代码"，界面就不再有价值。

约束（都踩过）：必须是 `//` 注释（否则编译不过）、必须**一行**（JSON 不能含换行）、
解析失败必须优雅降级到代码模式（手改过的文件也要能打开）、未知字段要原样保留
（旧版本页面打开新版本文件再保存，不能丢数据）。

### 2.3 关键设计：离线编译验证器可信吗

`verify_compile.ps1` 用 Visual Studio 的 Roslyn `csc.exe`，参数**逐项对齐**游戏内
`CompilationManager` + `compiler.config.json`：

| 项 | 值 | 来源 |
| --- | --- | --- |
| references | mscorlib / System / System.Core / System.Data / KarstPatcher / KarstYRpp / KarstExtension | `compiler.config.json` + 源码里硬编码的 KarstPatcher/mscorlib |
| define | `REALTIME_INI` | `preprocessor_symbols` |
| 选项 | `/target:library /unsafe /optimize- /warn:4 /langversion:latest /nostdlib+` | `CompilationManager` 的 `CSharpCompilationOptions` |

**证据**：用它编译 `KarstPatcher\Scripts\MyFirst.cs`，产出的程序集与游戏自己编译出来的
那份**长度完全一致（4608 字节）**。所以它是等价代理，不是近似。

---

## 3. 命令

> ⚠️ 这些 `.ps1` 里含中文，**必须保存为 UTF-8 with BOM**。
> Windows PowerShell 5.1 会按 ANSI 读 `.ps1`，无 BOM 时中文注释会被解成乱码并**报语法错误**
> （表现为 "The '<' operator is reserved for future use" 之类完全不相干的错）。
> `verify_compile.ps1` 是纯 ASCII，不受影响。
>
> 另外本机执行策略是 Restricted，所以要带 `-ExecutionPolicy Bypass`。

```powershell
# 1) 构建单文件页面（改了 blocks\*.js 或 template 之后必须跑）
powershell -ExecutionPolicy Bypass -File Karst\tools\script-studio\build_studio.ps1

# 2) 积木库测试：逐块生成 .cs -> 离线编译 -> 元数据往返 -> 校验器行为
node Karst\tools\script-studio\tests\run_tests.js --compile

# 3) 构建产物静态检查（内联脚本语法 / DOM id / 注入数据）
node Karst\tools\script-studio\tests\check_page.js

# 4) 单独编译某个目录下的 .cs
powershell -ExecutionPolicy Bypass -File Karst\tools\script-studio\verify_compile.ps1 -PathDir <目录>

# 5) 重新生成演示脚本（并可选部署到测试游戏）
node Karst\tools\script-studio\tests\make_demo.js
node Karst\tools\script-studio\tests\make_demo.js --deploy "E:\_game\RA2\yr_karst_test\KarstPatcher\Scripts"
```

改了积木库之后的**固定顺序**：`build_studio.ps1` → `run_tests.js --compile` → `check_page.js`。
三个都必须通过。

---

## 4. 当前积木覆盖

| 类别 | 积木 |
| --- | --- |
| **事件**（5） | 每帧 `OnUpdate`、开火 `OnFire`、受伤 `OnReceiveDamage`、出场 `OnPut`、移除 `OnRemove` |
| **条件**（9） | 每 N 帧、概率 N%、自己血量低于/高于 N%、自己的类型是 X、自己的类型在列表里、目标是单位/建筑、记住的目标还在、本次伤害大于 N |
| **动作**（11） | 打日志、记录自己的坐标/血量、记录自己的类型 ID、修改自己的血量、修改即将受到的伤害、在自己位置造成范围伤害、向目标发射武器、记住目标、忘记目标、打击记住的目标、记录记住的目标的坐标 |
| **状态** | `_rememberedTarget`（`ExtensionReference<TechnoExt>`，由"记住目标"自动声明） |

**范围说明**：这一版是"标准范围"（日志/数值/伤害与爆炸/计时与概率/目标引用），
目标是全量。尚未做的：激光特效、动画、超武、单位移动/朝向/变形、装饰器状态、
子弹脚本（`BulletScriptable` 的绑定方式本身还没验证过）。

**刻意不做的**：读自定义 INI（`INIReader` 在脚本侧没有入口）、音效（引擎侧根本没有
可用的 `VocClass`/`PlaySound` API）—— 做了会生成编译不过或静默失效的代码。

---

## 5. 踩过的坑（会重复踩）

1. **PowerShell 脚本必须 UTF-8 with BOM**（见 §3）。现象是报一堆与中文无关的语法错误。
   现有 `tools\script-editor\build_editor.ps1` 就是带 BOM 的。
2. **内联顺序 = 依赖顺序**。`codegen.js` 在浏览器里是**加载时**取
   `root.KarstMetadata`；把它排在 `metadata.js` 前面，构建照样"成功"，但页面第一次
   生成代码就抛 `Cannot read properties of undefined (reading 'encode')`。
   现在 `check_page.js` 会真的跑一遍内联库，专门抓这个。
3. **块的局部变量名必须带块 id，不能带事件 id**。用事件 id 时，同一事件里放两个
   `changeHealth` 会生成两个 `hp_e1` → `error CS0128`。夹具里的"重复同类积木"用例
   就是为这个写的。
4. **`needs` 写漏 = 编译不过**。积木代码里用到 `self`、`pTarget`、`_rememberedTarget`
   却没在 `needs` 里声明，生成的方法体就没有对应的声明/字段。
5. **校验器不要用 `edef.id`**：事件定义是按 id 做键的，对象里**没有** `id` 字段，
   读出来是 undefined，会让所有"限定事件"的积木被误判为放错位置。
6. **`-File` 传数组参数时逗号不是分隔符**：`powershell -File x.ps1 -Path a.cs,b.cs` 会把
   整个 `a.cs,b.cs` 当成一个路径。`verify_compile.ps1` 因此自己按 `,`/`;` 拆分。

---

## 6. 产物与源码的对应

| 你想改什么 | 改哪里 |
| --- | --- |
| 某个积木生成的 C# | `blocks\registry.js` 里那个块的 `emit` / `expr` |
| 加一个新积木 | `blocks\registry.js` 加定义（含 `needs` / `args` / `events`），重跑测试 |
| 界面布局 / 交互 | `studio.template.html` |
| 注入哪些游戏数据 | `build_studio.ps1` 的 `Get-RulesInventory` / `$listSections` |
| 编译参数对齐 | `verify_compile.ps1` 顶部的 `$refs` 与开关 |

改完积木库**一定**要重跑 `run_tests.js --compile`：夹具是自动生成的，新块会自动被覆盖到。
