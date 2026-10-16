# Karst 用户脚本可调用 API 清单（草稿）

> **用途**：给可视化积木库做"签名字典"。每条都尽量给出**可直接照抄的完整签名**，
> 并标注 **来源文件:行号** 以便复核。
> **边界**：只写源码里**确实存在**的东西；不存在的一律在 §10 明确列出。
> 我读的是 `Karst\refs\karstpatcher\` 下的源码（= 构建 `KarstPatcher.dll` /
> `KarstYRpp.dll` / `KarstExtension.dll` 的那份），不是 NuGet 包，也不是旧版
> 上游旧版样本。
>
> 标注约定：
> * ✅ **已核实**：源码里逐字确认过签名。
> * ⚠️ **有坑**：能用但极易崩 / 行为反直觉。
> * ❌ **不存在**：不要生成。
> * ❓ **未确认**：源码里没读透或缺少调用点，不敢保证运行时行为。

---

## 0. 程序集 / 命名空间 / 文件对照

| 程序集 | 项目文件 | 根命名空间 | 源码目录 |
| --- | --- | --- | --- |
| `KarstPatcher.dll` | `karstpatcher\karstpatcher.csproj` | `KarstPatcher` | `refs\karstpatcher\karstpatcher\` |
| `KarstYRpp.dll` | `PatcherYRpp\PatcherYRpp.csproj`（`<AssemblyName>KarstYRpp</AssemblyName>`，`PatcherYRpp.csproj:11`） | `PatcherYRpp`、`PatcherYRpp.Utilities` | `refs\karstpatcher\PatcherYRpp\` |
| `KarstExtension.dll` | `Extension\Extension.csproj`（`<AssemblyName>KarstExtension</AssemblyName>`，`Extension.csproj:11`） | `Extension.Ext` / `.Script` / `.Utilities` / `.Components` / `.Decorators` | `refs\karstpatcher\Extension\` |

三者都是 `TargetFrameworkVersion v4.8`（`Extension.csproj:12`、`PatcherYRpp.csproj:12`）。

**脚本里该抄的 using**（已核实全部必需项，见 §1.0）：

```csharp
using System;
using System.Collections.Generic;
using KarstPatcher;              // Logger
using Extension.Ext;               // TechnoExt / BulletExt / TechnoTypeExt / BulletTypeExt / AnimTypeExt / Container<,>
using Extension.Script;            // TechnoScriptable / BulletScriptable / ScriptManager / Script / ScriptComponent
using Extension.Utilities;         // ExtensionReference / INIReader / INI_EX / ContainerHelper
using Extension.Components;        // Component / ExtComponent<T>（用 GetComponent<T>() 时要）
using Extension.Decorators;        // EventDecorator / Decorator / DecoratorId / DecoratorComponent
using PatcherYRpp;                 // Pointer<T> / CoordStruct / 引擎类
using PatcherYRpp.Utilities;       // MathEx
```

> ⚠️ `ExtensionReference<T>` 在 **`Extension.Utilities`**，`MathEx` 在 **`PatcherYRpp.Utilities`**。
> 这两个是 `Karst\docs\已有功能0.1.0.md` 明确记录过的历史翻车点。

---

## 1. 脚本基类与 `Owner` 上的 public 成员

### 1.0 接口与基类（`Extension\Script\Scriptable.cs`）

```csharp
// Scriptable.cs:13-16
public interface IAbstractScriptable { public void OnUpdate(); }

// Scriptable.cs:17-23
public interface IObjectScriptable : IAbstractScriptable
{
    public void OnPut(CoordStruct coord, Direction faceDir);
    public void OnRemove();
    public void OnReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter,
        Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker,
        bool IgnoreDefenses, bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse);
}

// Scriptable.cs:24-27
public interface ITechnoScriptable : IObjectScriptable
{
    public void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex);
}

// Scriptable.cs:28-31
public interface IBulletScriptable : IObjectScriptable
{
    public void OnDetonate(Pointer<CoordStruct> pCoords);
}

// Scriptable.cs:33-35  —— 注意：IScriptable 只要求 IReloadable
public interface IScriptable : IReloadable { }
```

**`Scriptable<T>`（`Scriptable.cs:37-45`）**

```csharp
[Serializable]
public abstract class Scriptable<T> : ScriptComponent, IScriptable
{
    public T Owner { get; protected set; }      // ⚠️ 只有 protected set，脚本里不要赋值
    public Scriptable(T owner);
}
```

**两个可直接继承的脚本基类**

```csharp
// Scriptable.cs:47-61
[Serializable]
public class TechnoScriptable : Scriptable<TechnoExt>, ITechnoScriptable
{
    public TechnoScriptable(TechnoExt owner);
    public virtual void OnPut(CoordStruct coord, Direction faceDir) { }
    public virtual void OnRemove() { }
    public virtual void OnReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter,
        Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker,
        bool IgnoreDefenses, bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse) { }
    public virtual void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex) { }
}

// Scriptable.cs:63-77
[Serializable]
public class BulletScriptable : Scriptable<BulletExt>, IBulletScriptable
{
    public BulletScriptable(BulletExt owner);
    public virtual void OnPut(CoordStruct coord, Direction faceDir) { }
    public virtual void OnRemove() { }
    public virtual void OnReceiveDamage(/* 同 TechnoScriptable 的 7 参数签名 */) { }
    public virtual void OnDetonate(Pointer<CoordStruct> pCoords) { }
}
```

> ⚠️ **`OnUpdate()` 不在这两个类的源码里显式声明**（`Scriptable.cs` 里搜不到）。
> 它来自祖父类 `Component.OnUpdate()`（`Components\Component.cs:286`，`public virtual void OnUpdate()`）。
> 所以 `public override void OnUpdate()` 是合法的，但**生成器不要写成 `override` 之外的形式**
> （比如不要试图 `new`，也不要给 `OnUpdate` 加参数）。

**`ScriptComponent`（`Script\ScriptComponent.cs:10-24`）**

```csharp
[Serializable]
public abstract class ScriptComponent : Component
{
    public ScriptComponent();
    public ScriptComponent(Script script);
    public Script Script { get; internal set; }    // ⚠️ internal set
}
```

### 1.1 `TechnoExt`（`Extension\Ext\TechnoExt.cs`）

```csharp
namespace Extension.Ext
{
    [Serializable]
    public partial class TechnoExt : Extension<TechnoClass>, IHaveComponent
    {
        // 寄存器：单位指针 -> TechnoExt
        public static Container<TechnoExt, TechnoClass> ExtMap;      // 字段，行 21

        // 类型扩展（懒加载：第一次访问时用 OwnerObject.Ref.Type 建/取）
        public TechnoTypeExt Type { get; }                            // 行 24-35

        public ExtComponent<TechnoExt> ExtComponent { get; }          // 行 39（⚠️ 会唤醒组件）
        public DecoratorComponent DecoratorComponent { get; }         // 行 40
        public Component AttachedComponent { get; }                   // 行 41（= ExtComponent，⚠️ 会唤醒）
        public Component AttachedComponentIfAwaked { get; }           // 行 44-45（Karst 新增，不唤醒）
        public bool OnPutFired;                                       // 行 54（public 字段，Karst 新增）

        public TechnoExt(Pointer<TechnoClass> OwnerObject);           // 行 56

        // 从基类 Extension<TechnoClass> 继承来的（见 §1.4）
        public Pointer<TechnoClass> OwnerObject { get; set; }
    }
}
```

**积木库可以直接用的**：`Owner.OwnerObject`、`Owner.Type`、`Owner.DecoratorComponent`、`Owner.ExtComponent`。

### 1.2 `BulletExt`（`Extension\Ext\BulletExt.cs`）

```csharp
namespace Extension.Ext
{
    [Serializable]
    public partial class BulletExt : Extension<BulletClass>, IHaveComponent
    {
        public static Container<BulletExt, BulletClass> ExtMap;       // 行 21
        public BulletTypeExt Type { get; }                            // 行 24-35
        public ExtComponent<BulletExt> ExtComponent { get; }           // 行 39
        public DecoratorComponent DecoratorComponent { get; }          // 行 40
        public Component AttachedComponent { get; }                    // 行 41
        public Component AttachedComponentIfAwaked { get; }            // 行 44-45
        public BulletExt(Pointer<BulletClass> OwnerObject);            // 行 47
        public Pointer<BulletClass> OwnerObject { get; set; }          // 继承
    }
}
```

> `BulletExt` **没有** `OnPutFired` 字段（只有 `TechnoExt` 有，`TechnoExt.cs:54`）。

### 1.3 `TechnoTypeExt` / `BulletTypeExt` / `AnimTypeExt`

```csharp
// Ext\TechnoTypeExt.cs:18-27
[Serializable]
public partial class TechnoTypeExt : Extension<TechnoTypeClass>
{
    public static Container<TechnoTypeExt, TechnoTypeClass> ExtMap;   // 行 20
    public List<Script.Script> Scripts;                                // 行 22  —— public 字段
    public TechnoTypeExt(Pointer<TechnoTypeClass> OwnerObject);        // 行 24
    public Pointer<TechnoTypeClass> OwnerObject { get; set; }          // 继承
}

// Ext\BulletTypeExt.cs:17-26
[Serializable]
public partial class BulletTypeExt : Extension<BulletTypeClass>
{
    public static Container<BulletTypeExt, BulletTypeClass> ExtMap;    // 行 19
    public List<Script.Script> Scripts;                                // 行 21
    public BulletTypeExt(Pointer<BulletTypeClass> OwnerObject);        // 行 23
}

// Ext\AnimTypeExt.cs:16-23
[Serializable]
public partial class AnimTypeExt : Extension<AnimTypeClass>
{
    public static Container<AnimTypeExt, AnimTypeClass> ExtMap;        // 行 18
    public AnimTypeExt(Pointer<AnimTypeClass> OwnerObject);            // 行 20
    // ⚠️ 没有 public Scripts 字段
}
```

### 1.4 `Extension<T>` 基类（`Extension\Ext\Container.cs:31-129`）

```csharp
[Serializable]
public abstract class Extension<T> : IExtension, IReloadable, IDeserializationCallback
{
    public Pointer<T> OwnerObject { get; set; }        // 行 36 —— ⚠️ 有 public setter
    public bool Expired { get; }                       // 行 47（= OwnerObject.IsNull）
    public Extension(Pointer<T> ownerObject);          // 行 39
    public void EnsureConstanted();                    // 行 70
    public void LoadFromINI(Pointer<CCINIClass> pINI); // 行 79
    public virtual void OnDeserialization(object sender);       // 行 56
    public virtual void LoadFromStream(IStream stream);         // 行 127
    public virtual void SaveToStream(IStream stream);           // 行 128
    public static implicit ... // 无
}
```

### 1.5 寄存器 `Container<TExt, TBase>`（`Extension\Ext\Container.cs:135-344`）

```csharp
public class Container<TExt, TBase> : IContainer where TExt : Extension<TBase>
{
    public IEnumerable<TExt> AllExtensions { get; }           // 行 144-152（Karst 新增，返回拷贝）
    public Container(string name);                            // 行 158
    public TExt FindOrAllocate(Pointer<TBase> key);           // 行 169
    public TExt Find(Pointer<TBase> key);                     // 行 187 —— 找不到返回 null
    public void Remove(Pointer<TBase> key);                   // 行 206
    public void Clear();                                      // 行 218
    public void LoadAllFromINI(Pointer<CCINIClass> pINI);     // 行 227
    public void LoadFromINI(Pointer<TBase> key, Pointer<CCINIClass> pINI); // 行 235
}
```

**积木库最常见写法**（`Find` 返回 `null` 而不是空指针，判空用 `!= null`）：

```csharp
TechnoExt ext = TechnoExt.ExtMap.Find(pTechno);   // pTechno 是 Pointer<TechnoClass>
if (ext != null) { /* ... */ }
```

### 1.6 脚本是如何被驱动的（决定了哪些回调真的会跑）

| 回调 | 谁驱动 | 出处 | 状态 |
| --- | --- | --- | --- |
| `OnUpdate()` | `ScriptManager.DriveExtensions` 每帧 | `Script\ScriptManager.cs:395-446`（`Program.Tick += TickExtensions`，行 257） | ✅ |
| `OnPut(coord, faceDir)` | 第一次被驱动的帧（Karst 新增） | `ScriptManager.cs:266-290` + `423-427` | ✅ |
| `OnReceiveDamage(...)` | Hook `0x701900` | `Ext\TechnoExt.cs:122-142` → `ScriptManager.cs:295-306` | ✅ |
| `OnFire(...)` | Hook `0x6FDD50` | `Ext\TechnoExt.cs:144-157` → `ScriptManager.cs:378-393` | ✅ |
| `OnRemove()` | Hook `0x6F4500`（DTOR） | `Ext\TechnoExt.cs:159-199` → `ScriptManager.cs:311-318` | ✅ |
| `OnDetonate(pCoords)` | Hook `0x4690B0` | `Ext\BulletExt.cs:78-99` → `ScriptManager.cs:323-330` | ✅ |
| `OnRender()` | ❌ **没有任何调用点** | `Components\Component.cs:289` | ❌ |
| `OnDestroy()` | 只在 `Component.Destroy(component)` 里手动调用 | `Component.cs:370-374` | ⚠️ 脚本收不到 |
| `Start()` | 由 `ExtComponent<T>.OnUpdate` 通过 `EnsureStarted()` 驱动 | `Components\ExtComponent.cs:33-42` | ✅（但只在 ExtComponent 上） |
| `Awake()` | 组件 `EnsureAwaked()` | `Component.cs:376-383` | ✅ |

> ⚠️ **`OnPut` 的 `faceDir` 永远是 `default`（= `Direction.N`）**：
> `ScriptManager.cs:271-278` 里明确写了"Facing 的取法各类不同，故留默认值"。
> 生成器不要把 `faceDir` 当成真实朝向。
>
> ⚠️ **`OnPutScripts` 只对 `TechnoExt` 调用**（`ScriptManager.cs:423-427`），
> `BulletScriptable.OnPut` 实际上永远不会被触发（❓ 未确认是否特意如此，但代码是这样）。

---

## 2. 自身单位读写：从 `Owner.OwnerObject.Ref` 出发

### 2.0 继承链与"多重 `Base`"怎么念

`TechnoClass` 里同时暴露了**同一地址**的不同视角（`TechnoClass.cs:417-420`）：

```csharp
[FieldOffset(0)] public RadioClass BaseRadio;
[FieldOffset(0)] public MissionClass BaseMission;
[FieldOffset(0)] public ObjectClass Base;         // ← 常用
[FieldOffset(0)] public AbstractClass BaseAbstract;
```

所以文档/旧样本里的 `Ref.Base.Base.ID` = `TechnoClass.Base`(ObjectClass) → `ObjectClass.Base`(AbstractClass)。
`AbstractClass` 里**没有** `ID`，`ID` 在 `AbstractTypeClass`（`AbstractTypeClass.cs:35-36`），于是完整路径是：

```csharp
Pointer<TechnoClass> pTechno = Owner.OwnerObject;

// 类型 ID（string）
string id = pTechno.Ref.Type.Ref.Base.Base.ID;
//             TechnoClass.Type ─┘     │    │    └─ AbstractTypeClass.ID  (AnsiStringPointer → string)
//                                     │    └────── AbstractTypeClass.Base (AbstractClass)
//                                     └─────────── ObjectTypeClass.Base → AbstractTypeClass

// 坐标（leptons）
CoordStruct pos = pTechno.Ref.Base.Base.GetCoords();
//                  ObjectClass ─┘   └─ AbstractClass   └─ AbstractClass.GetCoords()

// 血量
int hp = pTechno.Ref.Base.Health;                     // ObjectClass.Health
int maxHp = pTechno.Ref.Type.Ref.Base.Strength;       // ObjectTypeClass.Strength
```

**用 `Base` 视角时要注意自己算字段偏移**（都是 `FieldOffset(0)` 起算的联视图）：

| 想访问 | 写法 |
| --- | --- |
| `TechnoClass` 独有（Ammo / Target / Facing / Veterancy …） | `pTechno.Ref.XXX` |
| `FootClass` 独有（MoveTo / StopMoving / CurrentMapCoords …） | `pTechno.Ref.Base.XXX` 是**错的**，应 `CastToFoot` 或 `pFoot.Ref.XXX` |
| `ObjectClass` 的（Health / IsAlive / InLimbo / Location / Parachute …） | `pTechno.Ref.Base.XXX`（= `TechnoClass.Base`，类型 `ObjectClass`） |
| `MissionClass` 的（QueueMission / StartMission …） | `pTechno.Ref.BaseMission.XXX`（或 `Ref.Base.BaseMission.XXX`） |
| `AbstractClass` 的（GetCoords / IsDead / WhatAmI / GetOwningHouse …） | `pTechno.Ref.Base.Base.XXX` |

⚠️ **`TechnoClass` 本身没有 `BaseTechno` 这类字段**；`UnitClass`/`InfantryClass`/`BuildingClass` 才有
（`UnitClass.cs:13-18`、`InfantryClass.cs:13-18`、`BuildingClass.cs:17-21`）。
`TechnoClass` 里自己的 `Base` 就是 `ObjectClass`。

### 2.1 `TechnoClass` public 字段/属性（`PatcherYRpp\TechnoClass.cs`）

**状态（`Bool` 类型，可隐式转 `bool`）**

| 成员 | 类型 | 偏移 | 行 |
| --- | --- | --- | --- |
| `BaseRadio` | `RadioClass` | 0 | 417 |
| `BaseMission` | `MissionClass` | 0 | 418 |
| `Base` | `ObjectClass` | 0 | 419 |
| `BaseAbstract` | `AbstractClass` | 0 | 420 |
| `Flashing` | `FlashData` | 240 | 422 |
| `Animation` | `ProgressTimer` | 248 | 423 |
| `Passengers` | `PassengersClass` | 276 | 424 |
| `Transporter` | `Pointer<TechnoClass>` | 284 | 426 |
| `Veterancy` | `VeterancyStruct` | 336 | 428 |
| `DrainTarget` | `Pointer<TechnoClass>` | 460 | 431 |
| `DrainingMe` | `Pointer<TechnoClass>` | 464 | 433 |
| `DrainAnim` | `Pointer<AnimClass>` | 468 | 437 |
| `Owner` | `Pointer<HouseClass>` | 540 | 439-440 |
| `Berzerk` | `Bool` | 664 | 442 |
| `DirectRockerLinkedUnit` | `Pointer<FootClass>` | 680 | 449 |
| `LocomotorTarget` | `Pointer<FootClass>` | 684 | 451 |
| `LocomotorSource` | `Pointer<FootClass>` | 688 | 453 |
| `Target` | `Pointer<AbstractClass>` | 692 | 455 |
| `LastTarget` | `Pointer<AbstractClass>` | 696 | 456 |
| `CaptureManager` | `Pointer<CaptureManagerClass>` | 700 | 458 |
| `MindControlledBy` | `Pointer<TechnoClass>` | 704 | 460 |
| `MindControlledByAUnit` | `Bool` | 708 | 461 |
| `MindControlRingAnim` | `Pointer<AnimClass>` | 712 | 463 |
| `MindControlledByHouse` | `Pointer<AnimClass>` | 716 | 465（⚠️ 名字叫 House 但类型是 `AnimClass`，上游笔误） |
| `SpawnManager` | `Pointer<SpawnManagerClass>` | 720 | 467 |
| `SpawnOwner` | `Pointer<TechnoClass>` | 724 | 469 |
| `Ammo` | `int` | 764 | 471 |
| `AngleRotatedSideways` | `float` | 808 | 474 |
| `AngleRotatedForwards` | `float` | 812 | 475 |
| `RockingSidewaysPerFrame` | `float` | 816 | 479 |
| `RockingForwardsPerFrame` | `float` | 820 | 480 |
| `HijackerInfantryType` | `int` | 824 | 482 |
| `Tiberium` | `OwnedTiberiumStruct` | 828 | 484 |
| `BarrelFacing` | `FacingStruct` | 880 | 486 |
| `Facing` | `FacingStruct` | 904 | 487 |
| `TurretFacing` | `FacingStruct` | 928 | 488 |
| `CurrentBurstIndex` | `int` | 952 | 489 |
| `CountedAsOwned` | `Bool` | 972 | 491 |
| `IsSinking` | `Bool` | 973 | 492 |
| `WasSinkingAlready` | `Bool` | 974 | 493 |
| `HasBeenAttacked` | `Bool` | 977 | 495 |
| `Cloakable` | `Bool` | 978 | 496 |
| `IsPrimaryFactory` | `Bool` | 979 | 497 |
| `Spawned` | `Bool` | 980 | 498 |
| `IsInPlayfield` | `Bool` | 981 | 499 |
| `IsHumanControlled` | `Bool` | 1050 | 502 |
| `DiscoveredByPlayer` | `Bool` | 1051 | 503 |
| `DiscoveredByComputer` | `Bool` | 1052 | 504 |
| `SightIncrease` | `byte` | 1056 | 506 |
| `IsRadarTracked` | `Bool` | 1059 | 508 |
| `IsOnCarryall` | `Bool` | 1060 | 509 |
| `IsCrashing` | `Bool` | 1061 | 510 |
| `WasCrashingAlready` | `Bool` | 1062 | 511 |
| `IsBeingManipulated` | `Bool` | 1063 | 512 |
| `CurrentTargetThreatValues` | `DynamicVectorClass<int>` | 1088 | 514 |
| `CurrentTargets` | `DynamicVectorClass<Pointer<AbstractClass>>` | 1112 | 515 |
| `AttackedTargets` | `DynamicVectorClass<Pointer<AbstractClass>>` | 1136 | 518 |
| `ShouldLoseTargetNow` | `Bool` | 1292 | 520 |
| `Disguise` | `Pointer<ObjectTypeClass>` | 1304 | 522 |
| `DisguisedAsHouse` | `Pointer<HouseClass>` | 1308 | 523 |

**属性 / 静态**

```csharp
// TechnoClass.cs:188-189
static public readonly IntPtr ArrayPointer;                                        // 0xA8EC78
static public ref DynamicVectorClass<Pointer<TechnoClass>> Array { get; }          // 全局单位数组
public unsafe Pointer<TechnoTypeClass> Type { get; }                               // 行 191-198（读 0x6F3270）
```

> ⚠️ `TechnoClass.Type` 是**属性**（虚函数 0x6F3270），而 `UnitClass.Type` / `InfantryClass.Type` /
> `BuildingClass.Type` / `BulletClass.Type` 是**字段**。别混。

**public 方法（全部已核实）**

```csharp
// TechnoClass.cs:203-412
public unsafe bool                CanReachLocation(CoordStruct destCoords);                       // 203
public unsafe int                 SelectWeapon(Pointer<AbstractClass> pTarget);                   // 209
public unsafe int                 SelectNavalTargeting(Pointer<AbstractClass> pTarget);           // 214
public unsafe int                 GetROF(int idxWeapon);                                           // 220
public unsafe int                 GetGuardRange(int dwUnk);                                        // 225
public unsafe int                 DecreaseAmmo();                                                  // 231
public unsafe void                AddPassenger(Pointer<FootClass> pPassenger);                     // 236
public unsafe bool                IsCloseEnough(Pointer<AbstractClass> pTarget, int idxWeapon);    // 242
public unsafe bool                IsCloseEnoughToAttack(Pointer<AbstractClass> pTarget);           // 247
public unsafe bool                IsCloseEnoughToAttackCoords(CoordStruct coords);                 // 252
public unsafe bool                Destroyed(Pointer<ObjectClass> pKiller);                         // 258
public unsafe FireError           GetFireErrorWithoutRange(Pointer<AbstractClass> pTarget, int nWeaponIndex);            // 263
public unsafe FireError           GetFireError(Pointer<AbstractClass> pTarget, int nWeaponIndex, bool ignoreRange);      // 268
public unsafe Pointer<TechnoClass> SelectAutoTarget(TargetFlags TargetFlags, CoordStruct TargetCoord, bool OnlyTargetHouseEnemy); // 273
public unsafe void                SetTarget(Pointer<AbstractClass> pTarget);                       // 278
public unsafe Pointer<BulletClass> Fire(Pointer<AbstractClass> pTarget, int nWeaponIndex);          // 283（虚函数 243）
public unsafe Pointer<BulletClass> Fire_NotVirtual(Pointer<AbstractClass> pTarget, int idxWeapon);  // 295（直接调 0x6FDD50）
public unsafe void                Guard();                                                        // 302
public unsafe bool                SetOwningHouse(Pointer<HouseClass> pHouse, bool announce = true); // 307
public unsafe void                UpdateRockingState(CoordStruct rockingCoord, float rockerValue, bool halfEffect); // 312
public unsafe bool                Crash(Pointer<ObjectClass> pKiller);                              // 317
public unsafe Pointer<WeaponStruct> GetDeployWeapon();                                             // 323
public unsafe Pointer<WeaponStruct> GetTurretWeapon();                                             // 328
public unsafe Pointer<WeaponStruct> GetWeapon(int idxWeapon);                                      // 333
public unsafe bool                HasTurret();                                                     // 338
public unsafe bool                CanOccupyFire();                                                 // 343
public unsafe int                 GetOccupyRangeBonus();                                           // 348
public unsafe int                 GetOccupantCount();                                              // 353
public unsafe CoordStruct         GetTargetCoords();                                               // 359
public unsafe void                SetDestination(Pointer<AbstractClass> pDest, bool bUnk = true);  // 366
public unsafe bool                CanAttackOnTheMove();                                            // 372
public unsafe bool                IsMindControlled();                                              // 379
public unsafe bool                CanBePermMindControlled();                                       // 385
public unsafe void                SetTargetForPassengers(Pointer<AbstractClass> pTarget);          // 392
public unsafe Pointer<HouseClass> GetOriginalOwner();                                              // 398
public unsafe int                 GetDistanceToTarget(Pointer<AbstractClass> pTarget);             // 404
public unsafe void                EnteredOpenTopped(Pointer<TechnoClass> pWho);                     // 410
```

> ✅ `Owner.OwnerObject.Ref.Fire(target, 0)` —— `TechnoScriptable` 的 `OnFire` 里**不要在
> `OnFire` 内部再调 `Fire`**：`OnFire` 就是从 `TechnoClass::Fire_NotVirtual` 里派发的，
> 会无限递归。要用就 `SetTarget` + `QueueMission(Mission.Attack, true)`，或先记标志下一帧再 Fire。❓（未实测，但按 Hook 位置推理必然递归）

**`VeterancyStruct`（`TechnoClass.cs:10-94`）**

```csharp
public struct VeterancyStruct
{
    public float Veterancy;                       // 行 93
    public bool IsNegative();                     // 行 50
    public bool IsRookie();                       // 行 55
    public bool IsVeteran();                      // 行 61
    public bool IsElite();                        // 行 67
    public void Reset();                          // 行 73
    public void SetRookie(bool notReally = true); // 行 78
    public void SetVeteran(bool yesReally = true);// 行 83
    public void SetElite(bool yesReally = true);  // 行 88
}
```

**`PassengersClass`（`TechnoClass.cs:97-131`）**

```csharp
public struct PassengersClass
{
    public int NumPassengers;                                   // 行 99
    public Pointer<FootClass> FirstPassenger { get; set; }       // 行 101
    public unsafe void AddPassenger(Pointer<FootClass> pPassenger);        // 行 103
    public unsafe Pointer<FootClass> GetFirstPassenger();                  // 行 109
    public unsafe Pointer<FootClass> RemoveFirstPassenger();               // 行 114
    public unsafe int GetTotalSize();                                      // 行 120
    public unsafe int IndexOf(Pointer<FootClass> candidate);               // 行 126
}
```

**`FlashData` / `OwnedTiberiumStruct`**：`Update()`（`TechnoClass.cs:139`）、
`GetAmount(int)` / `GetTotalAmount()` / `AddAmount(float,int)` / `RemoveAmount(float,int)` /
`GetTotalValue()`（行 149-177）。

### 2.2 `ObjectClass`（`PatcherYRpp\ObjectClass.cs`）—— 血量 / 存活 / 坐标

```csharp
[StructLayout(LayoutKind.Explicit, Size = 172)]
public struct ObjectClass
{
    static public readonly IntPtr CurrentObjectsPointer;                        // 0xA8ECB8
    static public ref DynamicVectorClass<Pointer<ObjectClass>> CurrentObjects;  // 行 14
    static public readonly IntPtr ArrayPointer;                                 // 0xA8E360
    static public ref DynamicVectorClass<Pointer<ObjectClass>> Array;           // 行 17

    // --- 字段（节选最常用的） ---
    [FieldOffset(0)]   public AbstractClass Base;                 // 行 215
    [FieldOffset(44)]  public int FallRate;                       // 行 217
    public Pointer<ObjectClass> NextObject { get; set; }          // 行 219（偏移 48）
    [FieldOffset(100)] public int CustomSound;                    // 行 221
    [FieldOffset(104)] public Bool BombVisible;                    // 行 222
    [FieldOffset(108)] public int Health;        // ★ 当前血量  行 224
    [FieldOffset(112)] public int EstimatedHealth;                 // 行 225
    [FieldOffset(116)] public Bool IsOnMap;                        // 行 226
    [FieldOffset(128)] public Bool NeedsRedraw;                    // 行 228
    [FieldOffset(129)] public Bool InLimbo;      // ★ 行 229
    [FieldOffset(130)] public Bool InOpenToppedTransport;          // 行 230
    [FieldOffset(131)] public Bool IsSelected;                     // 行 231
    [FieldOffset(132)] public Bool HasParachute;                   // 行 232
    public Pointer<AnimClass> Parachute { get; set; }              // 行 235（偏移 136）
    [FieldOffset(140)] public Bool OnBridge;                       // 行 236
    [FieldOffset(141)] public Bool IsFallingDown;                  // 行 237
    [FieldOffset(142)] public Bool WasFallingDown;                 // 行 238
    [FieldOffset(143)] public Bool IsABomb;                        // 行 239
    [FieldOffset(144)] public Bool IsAlive;      // ★ 行 240
    [FieldOffset(148)] public Layer LastLayer;                     // 行 242
    [FieldOffset(152)] public Bool IsInLogic;                      // 行 243
    [FieldOffset(153)] public Bool IsVisible;                      // 行 244
    [FieldOffset(156)] public CoordStruct Location; // ★ 绝对坐标（leptons） 行 246

    // --- 方法 ---
    public unsafe Pointer<TechnoTypeClass> GetTechnoType();                          // 行 22（虚 33）
    public unsafe Pointer<ObjectTypeClass> GetObjectType();                          // 行 28（虚 34）
    public unsafe CoordStruct GetFLH(int idxWeapon, CoordStruct BaseCoords);          // 行 34
    public unsafe bool Remove();                                                     // 行 41
    public unsafe bool Put(CoordStruct coord, Direction faceDir);                     // 行 46
    public unsafe void Disappear(bool permanently);                                   // 行 51
    public unsafe void RegisterDestruction(Pointer<TechnoClass> Destroyer);           // 行 56
    public unsafe void RegisterKill(Pointer<HouseClass> Destroyer);                   // 行 61
    public unsafe bool SpawnParachuted(CoordStruct coord);                            // 行 66
    public unsafe void DropAsBomb();                                                 // 行 71
    public unsafe void MarkAllOccupationBits(CoordStruct coord);                      // 行 76
    public unsafe void UnmarkAllOccupationBits(CoordStruct coord);                    // 行 81
    public unsafe void UnInit();                                                     // 行 86
    public unsafe void Reveal();                                                     // 行 91
    public unsafe void Undiscover();                                                 // 行 97
    public unsafe void See(uint dwUnk, uint dwUnk2);                                  // 行 102
    public unsafe bool UpdatePlacement(PlacementType placementType);                  // 行 107
    public unsafe void DrawRadialIndicator(uint dwUnk);                               // 行 113
    public unsafe void MarkForRedraw();                                              // 行 118
    public unsafe bool CanBeSelected();                                              // 行 123
    public unsafe bool CanBeSelectedNow();                                           // 行 128
    public unsafe bool ClickedAction(Action action, Pointer<ObjectClass> pTarget, bool bUnk); // 行 134
    public unsafe void Flash(int duration);                                          // 行 139
    public unsafe bool Select();                                                     // 行 144
    public unsafe void Deselect();                                                   // 行 149
    public unsafe Mission GetCurrentMission();                                        // 行 156（虚 97）
    public unsafe void RestoreMission(Mission mission);                               // 行 163（虚 98）

    // ★ 造成伤害（两条重载）
    public unsafe DamageState TakeDamage(int damage, Pointer<WarheadTypeClass> pWH, bool crewed);                        // 行 172
    public unsafe DamageState TakeDamage(int damage, Pointer<WarheadTypeClass> pWH,
        Pointer<ObjectClass> pAttacker, Pointer<HouseClass> pAttackingHouse, bool crewed);                              // 行 177
    public unsafe DamageState ReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter,
        Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker, bool IgnoreDefenses,
        bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse);                                              // 行 183

    public unsafe void Scatter(CoordStruct crd, bool ignoreMission, bool ignoreDestination);  // 行 190
    public unsafe bool DiscoveredBy(Pointer<HouseClass> pHouse);                     // 行 196
    public unsafe void SetLocation(CoordStruct coord);                               // 行 202
    public unsafe int GetHeight();                                                   // 行 208
}
```

> ⚠️ **没有 `SetHealth`**（搜过全仓：`SetHealth` 0 命中）。改血量只能：
> 直接写 `Ref.Base.Health = n;`（不触发引擎的死亡/红黄血判定），
> 或者用 `TakeDamage(damage, pWH, crewed)` 走正常受伤流程。
>
> ⚠️ **`TakeDamage(int, ...)` 实现上有悬垂指针风险**：`ObjectClass.cs:177-180` 是
> `ReceiveDamage(Pointer<int>.AsPointer(ref damage), ...)`——把**局部变量** `damage` 的地址传下去。
> 若引擎把该 `int*` 存起来延后使用，就是野指针。**保险写法**：自己 `YRMemory.Allocate<int>()`
> 或用一个字段级变量持有这个 `int` 再传 `Pointer<int>`。

### 2.3 `AbstractClass`（`PatcherYRpp\AbstractClass.cs`）—— 坐标 / 死亡判定

```csharp
[StructLayout(LayoutKind.Explicit, Size = 36)]
public struct AbstractClass
{
    static public readonly IntPtr ArrayPointer;                                   // 0xA8E360
    static public ref DynamicVectorClass<Pointer<ObjectClass>> Array;             // 行 15

    public unsafe void Init();                                                     // 行 17（虚 9）
    public unsafe void PointerExpired(Pointer<AbstractClass> pAbstract, bool removed); // 行 22（虚 10）
    public unsafe AbstractType WhatAmI();                                          // 行 27（虚 11）★ 判类型
    public unsafe int Size();                                                      // 行 32（虚 12）
    public unsafe void CalculateChecksum(Pointer<Checksummer> checksum);            // 行 37（虚 13）
    public unsafe int GetOwningHouseIndex();                                       // 行 42（虚 14）
    public unsafe Pointer<HouseClass> GetOwningHouse();                            // 行 47（虚 15）
    public unsafe int GetArrayIndex();                                             // 行 52（虚 16）
    public unsafe bool IsDead();                                                   // 行 57（虚 17）★
    public unsafe CoordStruct GetCoords();                                          // 行 62（虚 18）★
    public unsafe CoordStruct GetDestination(Pointer<TechnoClass> pDocker = default); // 行 70（虚 19）
    public unsafe bool IsOnFloor();                                                // 行 78（虚 20）
    public unsafe bool IsInAir();                                                  // 行 83（虚 21）
    public unsafe void Update();                                                   // 行 89（虚 23）
    public unsafe void AnnounceExpiredPointer(bool removed = true);                 // 行 96

    [FieldOffset(0)]  public IntPtr Vfptr;                                          // 行 102
    [FieldOffset(16)] public int UniqueID;                                          // 行 104
    [FieldOffset(20)] public AbstractFlags AbstractFlags;                           // 行 105 ★（判 Techno/Foot 用）
    [FieldOffset(24)] public int unknown_18;                                        // 行 106
    [FieldOffset(28)] public int RefCount;                                          // 行 107
    [FieldOffset(32)] public Bool Dirty;                                            // 行 108
}
```

> ⚠️ **`isDead` / `GetCenterCoords` 的名字**：
> * 存活判定叫 **`IsDead()`**（`AbstractClass.cs:57`），**不是** `isDead` 属性。
> * **没有 `GetCenterCoords()` on TechnoClass/ObjectClass/AbstractClass**。
>   全仓唯一命中在 `CellClass.cs:141`（格子中心）。想要"单位中心"用 `GetCoords()`，
>   或 `CellClass.Coord2Cell(...)` 取格子再 `cell.Ref.GetCenterCoords()`。

### 2.4 `FootClass`（`PatcherYRpp\FootClass.cs`）—— 移动

```csharp
public struct FootClass
{
    public unsafe bool MoveTo(CoordStruct where);                       // 行 14（虚 319）
    public unsafe bool StopDrive();                                     // 行 20（虚 320）
    public unsafe bool ChronoWarpTo(CoordStruct where);                 // 行 26（虚 322）
    public unsafe void Panic();                                         // 行 32（虚 326）
    public unsafe void UnPanic();                                       // 行 38（虚 327）
    public unsafe int  GetCurrentSpeed();                               // 行 44（虚 334）
    public unsafe bool StopMoving();                                    // 行 50（虚 338）
    public unsafe void AbortMotion();                                   // 行 56（0x4DF0D0）
    public unsafe bool Jumpjet_LocationClear();                         // 行 62
    public unsafe void Jumpjet_OccupyCell(CellStruct cell);             // 行 68

    [FieldOffset(0)]    public TechnoClass Base;                        // 行 74  ★
    [FieldOffset(0)]    public ObjectClass BaseObject;                  // 行 77
    [FieldOffset(0)]    public AbstractClass BaseAbstract;              // 行 78
    [FieldOffset(1368)] public CellStruct CurrentMapCoords;             // 行 80
    [FieldOffset(1372)] public CellStruct LastMapCoords;                // 行 81
    [FieldOffset(1376)] public CellStruct LastJumpjetMapCoords;         // 行 82
    [FieldOffset(1384)] public CellStruct CurrentJumpjetMapCoords;      // 行 83
    [FieldOffset(1408)] public double SpeedMultiplier;                  // 行 84
    [FieldOffset(1444)] public Pointer<AbstractClass> Destination;      // 行 85
    [FieldOffset(1448)] public Pointer<AbstractClass> LastDestination;  // 行 86
    [FieldOffset(1540)] public int PathDirections;                      // 行 87
    [FieldOffset(1600)] public TimerStruct PathDelayTimer;              // 行 88
    [FieldOffset(1616)] public TimerStruct BaseAttackTimer;             // 行 89
    [FieldOffset(1628)] public TimerStruct SightTimer;                  // 行 90
    [FieldOffset(1640)] public TimerStruct BlockagePathTimer;           // 行 91
    public ILocomotion Locomotor { get; set; }                          // 行 97-105
}
```

### 2.5 `MissionClass`（`PatcherYRpp\MissionClass.cs`）—— 指令

```csharp
public struct MissionClass
{
    public unsafe bool QueueMission(Mission mission, bool start_mission);        // 行 60（虚 122）
    public unsafe bool NextMission();                                           // 行 65（虚 123）
    public unsafe bool ForceMission(Mission mission);                           // 行 70（虚 124）
    public unsafe void StartMission(Mission mission, Pointer<AbstractClass> pTarget,
                                    Pointer<AbstractClass> pDestination = default); // 行 75（虚 125）
    public unsafe bool Mission_Revert();                                        // 行 81
    public unsafe bool HasForcedMission();                                      // 行 86
    public unsafe bool CanDoNextMission();                                      // 行 91
    public unsafe int  Mission_Sleep();     // 行 95
    public unsafe int  Mission_Harmless();  // 行 99
    public unsafe int  Mission_Ambush();    // 行 103
    public unsafe int  Mission_Attack();    // 行 107
    public unsafe int  Mission_Capture();   // 行 111
    public unsafe int  Mission_Eaten();     // 行 115
    public unsafe int  Mission_Guard();     // 行 119
    public unsafe int  Mission_AreaGuard(); // 行 123
    public unsafe int  Mission_Harvest();   // 行 127
    public unsafe int  Mission_Hunt();      // 行 131
    public unsafe int  Mission_Move();      // 行 135
    // …（后续到 223 行）
}
```

用法：`Owner.OwnerObject.Ref.BaseMission.QueueMission(Mission.Guard, true);`
（`Mission` 枚举在 `GeneralDefinitions.cs:335-370`）。

### 2.6 `RadioClass` / 单位子类

```csharp
// RadioClass.cs:10-41
[StructLayout(LayoutKind.Explicit, Size = 240)]
public struct RadioClass
{
    public unsafe RadioCommand SendToFirstLink(RadioCommand command);                              // 行 14（虚 157）
    public unsafe RadioCommand SendCommand(RadioCommand command, Pointer<TechnoClass> pRecipient);  // 行 19（虚 158）
    public unsafe RadioCommand SendCommandWithData(RadioCommand command,
        ref Pointer<AbstractClass> pInOut, Pointer<TechnoClass> pRecipient);                       // 行 24（虚 159）
    public unsafe void SendToEachLink(RadioCommand command);                                       // 行 29（虚 160）

    [FieldOffset(0)] public MissionClass Base;           // 行 36
    [FieldOffset(0)] public ObjectClass BaseObject;      // 行 37
    [FieldOffset(0)] public AbstractClass BaseAbstract;  // 行 38
}
```

> ⚠️ `RadioClass` **没有** `GetRadioCommand()` 之类的方法（旧样本里可能有）。
> 这 4 个方法的返回值/时序语义没有文档，❓ 未验证 —— **先别做成积木**。

// UnitClass.cs:11-23 —— 只有字段，没有方法
public struct UnitClass
{
    [FieldOffset(0)] public FootClass Base;
    [FieldOffset(0)] public TechnoClass BaseTechno;
    [FieldOffset(0)] public ObjectClass BaseObject;
    [FieldOffset(0)] public AbstractClass BaseAbstract;
    [FieldOffset(1732)] public Pointer<UnitTypeClass> Type;
}

// InfantryClass.cs:11-23
public struct InfantryClass
{
    [FieldOffset(0)] public FootClass Base;
    [FieldOffset(0)] public TechnoClass BaseTechno;
    // …
    [FieldOffset(1728)] public Pointer<InfantryTypeClass> Type;
}

// BuildingClass.cs:11-30
public struct BuildingClass : IOwnAbstractType<BuildingTypeClass>
{
    [FieldOffset(0)] public TechnoClass Base;
    [FieldOffset(1312)] public Pointer<BuildingTypeClass> Type;
    [FieldOffset(1632)] public Bool HasPower;
    [FieldOffset(1633)] public Bool IsOverpowered;
}
```

### 2.7 `TechnoTypeClass` / `ObjectTypeClass` / `AbstractTypeClass`（读单位"类型"上的数据）

```csharp
// TechnoTypeClass.cs:24-113
[StructLayout(LayoutKind.Explicit, Size = 3576)]
public struct TechnoTypeClass
{
    static public readonly IntPtr ArrayPointer;                                     // 0xA8EB00
    static public YRPP.GLOBAL_DVC_ARRAY<TechnoTypeClass> ABSTRACTTYPE_ARRAY;        // 行 28 ★ 按 ID 查类型

    public unsafe bool CanAttackMove();                                             // 行 31（虚 41）
    public unsafe bool CanCreateHere(CellStruct mapCoords, Pointer<HouseClass> pOwner); // 行 36（虚 42）
    public unsafe int  GetCost();                                                   // 行 41（虚 43）
    public unsafe int  GetRepairStepCost();                                         // 行 46（虚 44）
    public unsafe int  GetRepairStep();                                             // 行 51（虚 45）
    public unsafe int  GetRefund(Pointer<HouseClass> pHouse, bool bUnk);             // 行 56（虚 46）
    public unsafe int  GetFlightLevel();                                            // 行 61（虚 47）

    [FieldOffset(0)]    public ObjectTypeClass Base;                                // 行 73 ★
    [FieldOffset(0)]    public AbstractTypeClass BaseAbstractType;                  // 行 74
    [FieldOffset(844)]  public Guid Locomotor;                                      // 行 76
    [FieldOffset(896)]  public double Size;                                         // 行 78
    [FieldOffset(1460)] public MovementZone MovementZone;                           // 行 80
    [FieldOffset(1516)] public int Speed;                                            // 行 82
    [FieldOffset(1552)] public int Cost;                                             // 行 85
    [FieldOffset(1556)] public int Soylent;                                          // 行 86
    [FieldOffset(1660)] public SpeedType SpeedType;                                  // 行 88
    [FieldOffset(1776)] public Pointer<SHPStruct> Cameo;                             // 行 93
    [FieldOffset(1820)] public int ROT;                                              // 行 102
    public Pointer<WeaponStruct> Weapon { get; }                                     // 行 105（偏移 2200）
    [FieldOffset(2704)] public Bool ClearAllWeapons;                                 // 行 106
    public Pointer<WeaponStruct> EliteWeapon { get; }                                // 行 108（偏移 2708）
    [FieldOffset(3434)] public Bool BalloonHover;                                    // 行 110
    [FieldOffset(3476)] public Bool JumpJet;                                         // 行 111
}

// ObjectTypeClass.cs:11-37  —— 最大血量在这里
[StructLayout(LayoutKind.Explicit, Size = 660)]
public struct ObjectTypeClass
{
    public unsafe bool SpawnAtMapCoords(CellStruct mapCoords, Pointer<HouseClass> pOwner); // 行 13（虚 32）
    public unsafe Pointer<ObjectClass> CreateObject(Pointer<HouseClass> pOwner);           // 行 18（虚 35）
    [FieldOffset(0)]   public AbstractTypeClass Base;                              // 行 24 ★
    [FieldOffset(156)] public Armor Armor;                                          // 行 26
    [FieldOffset(160)] public int Strength;   // ★ 最大血量 行 27
    public AnsiStringPointer ImageFile { get; }                                     // 行 30
    public AnsiStringPointer AlphaImageFile { get; }                                 // 行 32
}

// AbstractTypeClass.cs:19-42
[StructLayout(LayoutKind.Explicit, Size = 152, Pack = 1)]
public struct AbstractTypeClass
{
    static public readonly IntPtr ArrayPointer;                                     // 0xA8E968
    static public YRPP.GLOBAL_DVC_ARRAY<AbstractTypeClass> ABSTRACTTYPE_ARRAY;       // 行 24
    public unsafe bool LoadFromINI(Pointer<CCINIClass> pINI);                        // 行 26（虚 25）
    [FieldOffset(0)]  public AbstractClass Base;                                    // 行 33
    public AnsiStringPointer ID { get; }      // ★ 类型 ID  行 36（偏移 36）
    [FieldOffset(61)] public byte UINameLabel_first;                                // 行 38
    [FieldOffset(96)] public UniStringPointer UIName;                                // 行 39
    [FieldOffset(100)] public byte Name_first;                                       // 行 41
}

// TechnoTypeClass.cs:12-21
public struct WeaponStruct
{
    public Pointer<WeaponTypeClass> WeaponType { get; set; }   // 行 16
    public CoordStruct FLH;                                    // 行 17
    public int BarrelLength;                                   // 行 18
    public int BarrelThickness;                                // 行 19
    public bool TurretLocked;                                  // 行 20
}
```

---

## 3. 对别的对象操作

### 3.1 `Pointer<T>`（`PatcherYRpp\Helpers\Pointer.cs:14-132`）—— ✅ 全部核实

```csharp
public unsafe struct Pointer<T>
{
    public static readonly Pointer<T> Zero;                   // 行 16

    public Pointer(int value);                                // 行 19
    public Pointer(long value);                               // 行 24
    public Pointer(void* value);                              // 行 29
    public Pointer(IntPtr value);                             // 行 34

    public void* Value;                                       // 行 39（public 字段！）
    public ref T Ref { get; }                                 // 行 40  ★ 解引用（不判空会崩）
    public T Data { get; set; }                               // 行 45  ★ 值拷贝（可写）
    public ref T this[int index] { get; }                     // 行 52

    public bool IsNull { get; }                               // 行 58  ★
    public bool IsNotNull { get; }                            // 行 63  ★

    public Pointer<TTo> Convert<TTo>();                       // 行 70  ★
    public Pointer<TTo> Cast<TTo>();                          // 行 75（= Convert）
    public IntPtr RawOffset(int offset);                      // 行 81

    public static Pointer<T> AsPointer(ref T obj);            // 行 87  ★（传 ref 给需要 Pointer<T> 的 API）
    public static int TypeSize();                             // 行 92

    public static Pointer<T> operator +(Pointer<T> ptr, int val);   // 行 99  ★（按元素偏移）
    public static Pointer<T> operator -(Pointer<T> ptr, int val);   // 行 101
    public static long operator -(Pointer<T> a, Pointer<T> b);      // 行 103（元素个数差）
    public static bool operator ==(Pointer<T> a, Pointer<T> b);     // 行 107 ★
    public static bool operator !=(Pointer<T> a, Pointer<T> b);     // 行 109
    public override int GetHashCode();                              // 行 110
    public override bool Equals(object obj);                        // 行 112（⚠️ 强转，obj 不是 Pointer<T> 会抛）
    public override string ToString();                              // 行 113（十六进制地址）

    public static explicit operator int(Pointer<T> value);          // 行 116 ★ 取地址
    public static explicit operator void*(Pointer<T> value);        // 行 118
    public static explicit operator long(Pointer<T> value);         // 行 120
    public static implicit operator IntPtr(Pointer<T> value);       // 行 122
    public static explicit operator Pointer<T>(void* value);        // 行 125
    public static explicit operator Pointer<T>(int value);          // 行 127
    public static explicit operator Pointer<T>(long value);         // 行 129
    public static implicit operator Pointer<T>(IntPtr value);       // 行 131
}
```

> ⚠️ **`Pointer<T>` 没有 `.Raw`**（旧样本里的 `.Raw` 不存在）。要整型地址用
> `(int)ptr` 或 `(long)ptr`（显式转换，行 116/120），或直接读 public 字段 `ptr.Value`。
>
> ⚠️ **`Ref` 不做判空**：`Unsafe.AsRef<T>(null)` 返回一个位于地址 0 的引用，
> 读它必然 `AccessViolationException`（不可 catch，直接崩游戏）。**任何 `Ref` 前必须 `IsNull` 检查**。
>
> ⚠️ `Pointer<T>.TypeSize()` 用的是 `Unsafe.SizeOf<T>()`；`T` 是 `Bool`/结构体时是**该字段类型**
> 的大小，不是指针大小。`ptrs[1]` 这类索引不要随手用。

### 3.2 类型转换 helper：`PatcherYRpp.Cast`（`PatcherYRpp\Helpers\Cast.cs`）—— 全部是扩展方法

```csharp
public static class Cast
{
    // 行 13 / 24
    public static bool CastToTechno(this Pointer<AbstractClass> pAbstract, out Pointer<TechnoClass> pTechno);
    public static bool CastToTechno(this Pointer<ObjectClass>  pObject,   out Pointer<TechnoClass> pTechno);

    // 行 30
    public static bool CastToObject(this Pointer<AbstractClass> pAbstract, out Pointer<ObjectClass> pObject);

    // 行 42 / 53 / 58
    public static bool CastToFoot(this Pointer<AbstractClass> pAbstract, out Pointer<FootClass> pFoot);
    public static bool CastToFoot(this Pointer<ObjectClass>  pObject,   out Pointer<FootClass> pFoot);
    public static bool CastToFoot(this Pointer<TechnoClass>  pTechno,   out Pointer<FootClass> pFoot);

    // 行 64 / 76 / 81 / 86
    public static bool CastIf<To>(this Pointer<AbstractClass> pAbstract, AbstractType type, out Pointer<To> ptr);
    public static bool CastIf<To>(this Pointer<ObjectClass>  pObject,   AbstractType type, out Pointer<To> ptr);
    public static bool CastIf<To>(this Pointer<TechnoClass>  pTechno,   AbstractType type, out Pointer<To> ptr);
    public static bool CastIf<To>(this Pointer<FootClass>    pFoot,     AbstractType type, out Pointer<To> ptr);

    // 行 92
    public static bool CastToCell(this Pointer<AbstractClass> pAbstract, out Pointer<CellClass> pCell);
}
```

标准用法（**唯一的正确判型方式**，别用 `WhatAmI() == AbstractType.Techno` 硬编码）：

```csharp
if (pTarget.CastToTechno(out Pointer<TechnoClass> pTechno))
{
    CoordStruct pos = pTechno.Ref.Base.Base.GetCoords();
}
```

### 3.3 `GLOBAL_DVC_ARRAY<T>`（`PatcherYRpp\YRPP.cs:15-64`）—— 全局类型表

```csharp
public class GLOBAL_DVC_ARRAY<T> : IEnumerable<Pointer<T>>
{
    public Pointer<DynamicVectorClass<Pointer<T>>> Pointer;         // 行 17
    public ref DynamicVectorClass<Pointer<T>> Array { get; }        // 行 18
    public GLOBAL_DVC_ARRAY(IntPtr pVector);                        // 行 20

    public Pointer<T> Find(string ID);                              // 行 25  ★ 找不到返回 Pointer<T>.Zero
    public int FindIndex(string ID);                                // 行 36  ★ 找不到返回 -1
    public IEnumerator<Pointer<T>> GetEnumerator();                 // 行 52  ★ 可 foreach
}

// 扩展方法（YRPP.cs:57-64）
public static Pointer<T>[] Finds<T>(this GLOBAL_DVC_ARRAY<T> dvc, IEnumerable<string> ts);
public static int[] FindIndexes<T>(this GLOBAL_DVC_ARRAY<T> dvc, IEnumerable<string> ts);
```

### 3.4 `DynamicVectorClass<T>`（`PatcherYRpp\ArrayClass.cs:14-133`）

```csharp
public struct DynamicVectorClass<T> : IEnumerable<T>
{
    static public ref DynamicVectorClass<T> GetDynamicVector(IntPtr ptr);   // 行 16
    public ref T this[int index] { get; }                                  // 行 21
    public ref T Get(int index);                                           // 行 23
    public Span<T> GetSpan();                                              // 行 25
    public unsafe bool OperatorEqual(Pointer<DynamicVectorClass<T>> pOther); // 行 31
    public unsafe bool SetCapacity(int capacity, Pointer<T> pMem = default); // 行 37
    public unsafe void Clear();                                            // 行 42
    public unsafe int FindItemIndex(Pointer<T> pItem);                     // 行 47
    public unsafe int GetItemIndex(Pointer<T> pItem);                      // 行 52
    public bool AddItem(T item);                                           // 行 58
    public bool AddUnique(Pointer<T> pItem);                               // 行 75
    public IEnumerator<T> GetEnumerator();                                 // 行 119

    public IntPtr Items;            // 行 127
    public int Capacity;            // 行 128
    public Bool IsInitialized;      // 行 129
    public Bool IsAllocated;        // 行 130
    public int Count;               // 行 131 ★
    public int CapacityIncrement;   // 行 132
}
```

标准遍历（`TechnoClass.Array` 是 `Pointer<TechnoClass>` 的矢量）：

```csharp
ref var arr = ref TechnoClass.Array;                 // TechnoClass.cs:189
for (int i = 0; i < arr.Count; i++)
{
    Pointer<TechnoClass> p = arr[i];
    if (p.IsNull) continue;
    // …
}
```

---

## 4. 攻击 / 伤害 / 子弹 / 爆炸

### 4.1 `WeaponTypeClass`（`PatcherYRpp\WeaponTypeClass.cs:11-84`）

```csharp
[StructLayout(LayoutKind.Explicit, Size = 352)]
public struct WeaponTypeClass
{
    static public readonly IntPtr ArrayPointer;                                       // 0x887568
    static public YRPP.GLOBAL_DVC_ARRAY<WeaponTypeClass> ABSTRACTTYPE_ARRAY;          // 行 15 ★

    [FieldOffset(0)]   public AbstractTypeClass Base;                                 // 行 17
    [FieldOffset(152)] public int AmbientDamage;                                      // 行 18
    [FieldOffset(156)] public int Burst;                                              // 行 19
    public Pointer<BulletTypeClass> Projectile { get; set; }                          // 行 20-21 ★（偏移 160）
    [FieldOffset(164)] public int Damage;         // ★ 行 23
    [FieldOffset(168)] public int Speed;                                              // 行 24
    [FieldOffset(172)] public Pointer<WarheadTypeClass> Warhead;                      // 行 25 ★
    [FieldOffset(176)] public int ROF;                                                // 行 26
    [FieldOffset(180)] public int Range;          // int(256 * ini)                    行 27
    [FieldOffset(184)] public int MinimumRange;                                       // 行 28
    [FieldOffset(188)] public DynamicVectorClass<int> Report;                         // 行 29
    [FieldOffset(216)] public DynamicVectorClass<int> DownReport;                     // 行 30
    [FieldOffset(244)] public DynamicVectorClass<Pointer<AnimTypeClass>> Anim;         // 行 31
    [FieldOffset(272)] public Pointer<AnimTypeClass> OccupantAnim;                    // 行 32
    [FieldOffset(276)] public Pointer<AnimTypeClass> AssaultAnim;                     // 行 33
    [FieldOffset(280)] public Pointer<AnimTypeClass> OpenToppedAnim;                  // 行 34
    [FieldOffset(284)] public Pointer<ParticleSystemTypeClass> AttachedParticleSystem; // 行 35
    [FieldOffset(288)] public ColorStruct LaserInnerColor;                            // 行 36
    [FieldOffset(291)] public ColorStruct LaserOuterColor;                            // 行 37
    [FieldOffset(294)] public ColorStruct LaserOuterSpread;                           // 行 38
    [FieldOffset(297)] public Bool UseFireParticles;                                  // 行 39
    [FieldOffset(298)] public Bool UseSparkParticles;                                 // 行 40
    [FieldOffset(299)] public Bool OmniFire;                                          // 行 41
    [FieldOffset(300)] public Bool DistributedWeaponFire;                             // 行 42
    [FieldOffset(301)] public Bool IsRailgun;                                         // 行 43
    [FieldOffset(302)] public Bool Lobber;                                            // 行 44
    [FieldOffset(303)] public Bool Bright;                                            // 行 45
    [FieldOffset(304)] public Bool IsSonic;                                           // 行 46
    [FieldOffset(305)] public Bool Spawner;                                           // 行 47
    [FieldOffset(306)] public Bool LimboLaunch;                                       // 行 48
    [FieldOffset(307)] public Bool DecloakToFire;                                     // 行 49
    [FieldOffset(309)] public Bool FireOnce;                                          // 行 51
    [FieldOffset(320)] public Bool InfiniteMindControl;                               // 行 59
    [FieldOffset(321)] public Bool FireWhileMoving;                                   // 行 60
    [FieldOffset(328)] public Bool Charges;                                           // 行 67
    [FieldOffset(329)] public Bool IsLaser;                                           // 行 68
    [FieldOffset(331)] public Bool IsLine;                                            // 行 70
    [FieldOffset(333)] public Bool IsHouseColor;                                      // 行 72
    [FieldOffset(334)] public char LaserDuration;                                     // 行 73
    [FieldOffset(336)] public Bool AreaFire;                                          // 行 75
    [FieldOffset(337)] public Bool IsElectricBolt;                                    // 行 76
    [FieldOffset(340)] public Bool IsRadBeam;                                         // 行 79
}
```

**统一写法**：

```csharp
Pointer<WeaponTypeClass> pWeapon = WeaponTypeClass.ABSTRACTTYPE_ARRAY.Find("RedEye2");
if (pWeapon.IsNull) return;              // ⚠️ 必须先判空
int dmg       = pWeapon.Ref.Damage;
int range     = pWeapon.Ref.Range;       // ⚠️ 是 256 倍
Pointer<WarheadTypeClass> pWH = pWeapon.Ref.Warhead;
Pointer<BulletTypeClass>  pBT = pWeapon.Ref.Projectile;
```

### 4.2 `WarheadTypeClass`（`PatcherYRpp\WarheadTypeClass.cs:11-69`）

```csharp
[StructLayout(LayoutKind.Explicit, Size = 464)]
public struct WarheadTypeClass
{
    static public readonly IntPtr ArrayPointer;                                       // 0x8874C0
    static public YRPP.GLOBAL_DVC_ARRAY<WarheadTypeClass> ABSTRACTTYPE_ARRAY;         // 行 15 ★

    [FieldOffset(0)]   public AbstractTypeClass Base;                                 // 行 17
    [FieldOffset(152)] public float Deform;                                           // 行 18
    [FieldOffset(292)] public float CellSpread;      // ★ 行 20
    [FieldOffset(296)] public float CellInset;                                        // 行 21
    [FieldOffset(300)] public float PercentAtMax;                                     // 行 22
    [FieldOffset(324)] public Bool Wall;      // 行 24
    [FieldOffset(325)] public Bool WallAbsoluteDestroyer;                             // 行 25
    [FieldOffset(326)] public Bool PenetratesBunker;                                  // 行 26
    [FieldOffset(327)] public Bool Wood;                                              // 行 27
    [FieldOffset(328)] public Bool Tiberium;                                          // 行 28
    [FieldOffset(330)] public Bool Sparky;                                            // 行 30
    [FieldOffset(331)] public Bool Sonic;                                             // 行 31
    [FieldOffset(332)] public Bool Fire;                                              // 行 32
    [FieldOffset(333)] public Bool Conventional;                                      // 行 33
    [FieldOffset(334)] public Bool Rocker;                                            // 行 34
    [FieldOffset(335)] public Bool DirectRocker;                                      // 行 35
    [FieldOffset(336)] public Bool Bright;                                            // 行 36
    [FieldOffset(340)] public Bool EMEffect;                                          // 行 40
    [FieldOffset(341)] public Bool MindControl;                                       // 行 41
    [FieldOffset(342)] public Bool Poison;                                            // 行 42
    [FieldOffset(343)] public Bool IvanBomb;                                          // 行 43
    [FieldOffset(344)] public Bool ElectricAssault;                                   // 行 44
    [FieldOffset(345)] public Bool Parasite;                                          // 行 45
    [FieldOffset(346)] public Bool Temporal;                                          // 行 46
    [FieldOffset(347)] public Bool IsLocomotor;                                       // 行 47
    [FieldOffset(348)] public Guid Locomotor;                                         // 行 48
    [FieldOffset(364)] public Bool Airstrike;                                         // 行 49
    [FieldOffset(365)] public Bool Psychedelic;                                       // 行 50
    [FieldOffset(366)] public Bool BombDisarm;                                        // 行 51
    [FieldOffset(368)] public int  Paralyzes;                                         // 行 52
    [FieldOffset(375)] public Bool Radiation;                                         // 行 56
    [FieldOffset(376)] public Bool PsychicDamage;                                     // 行 57
    [FieldOffset(377)] public Bool AffectsAllies;                                     // 行 58
    [FieldOffset(378)] public Bool Bullets;                                           // 行 59
    [FieldOffset(380)] public int  ShakeXlo;   // 行 61
    [FieldOffset(452)] public int  MaxDebris;  // 行 67
    [FieldOffset(456)] public int  MinDebris;  // 行 68
}
```

### 4.3 造子弹：`BulletTypeClass.CreateBullet`（`PatcherYRpp\BulletTypeClass.cs:11-73`）★ 唯一入口

```csharp
[StructLayout(LayoutKind.Explicit, Size = 760)]
public struct BulletTypeClass
{
    static public readonly IntPtr ArrayPointer;                                       // 0xA83C80
    static public YRPP.GLOBAL_DVC_ARRAY<BulletTypeClass> ABSTRACTTYPE_ARRAY;          // 行 15 ★

    public unsafe Pointer<BulletClass> CreateBullet(
        Pointer<AbstractClass> Target,      // ⚠️ 不参与底层调用，只在返回后写回 bullet.Target
        Pointer<TechnoClass>   Owner,
        int   Damage,
        Pointer<WarheadTypeClass> WH,
        int   Speed,
        bool  Bright);                                                        // 行 17-24

    [FieldOffset(0)]   public ObjectTypeClass Base;                                   // 行 26
    [FieldOffset(0)]   public AbstractTypeClass BaseAbstractType;                     // 行 27
    [FieldOffset(660)] public Bool Airburst;   // 行 29
    [FieldOffset(661)] public Bool Floater;    // 行 30
    [FieldOffset(662)] public Bool SubjectToCliffs;                                   // 行 31
    [FieldOffset(663)] public Bool SubjectToElevation;                                // 行 32
    [FieldOffset(664)] public Bool SubjectToWalls;                                    // 行 33
    [FieldOffset(665)] public Bool VeryHigh;   // 行 34
    [FieldOffset(666)] public Bool Shadow;     // 行 35
    [FieldOffset(667)] public Bool Arcing;     // 行 36
    [FieldOffset(668)] public Bool Dropping;   // 行 37
    [FieldOffset(669)] public Bool Level;      // 行 38
    [FieldOffset(670)] public Bool Inviso;     // 行 39
    [FieldOffset(671)] public Bool Proximity;  // 行 40
    [FieldOffset(672)] public Bool Ranged;     // 行 41
    [FieldOffset(673)] public Bool NoRotate;   // 行 42
    [FieldOffset(674)] public Bool Inaccurate; // 行 43
    [FieldOffset(675)] public Bool FlakScatter;// 行 44
    [FieldOffset(676)] public Bool AA;         // 行 45
    [FieldOffset(677)] public Bool AG;         // 行 46
    [FieldOffset(678)] public Bool Degenerates;// 行 47
    [FieldOffset(679)] public Bool Bouncy;     // 行 48
    [FieldOffset(684)] public int  Cluster;    // 行 52
    [FieldOffset(688)] public Pointer<WeaponTypeClass> AirburstWeapon;                 // 行 53
    [FieldOffset(692)] public Pointer<WeaponTypeClass> ShrapnelWeapon;                 // 行 54
    [FieldOffset(696)] public int  ShrapnelCount;                                      // 行 55
    [FieldOffset(700)] public int  DetonationAltitude;                                 // 行 56
    [FieldOffset(704)] public Bool Vertical;   // 行 57
    [FieldOffset(712)] public double Elasticity;                                       // 行 59
    [FieldOffset(720)] public int  Acceleration;                                       // 行 60
    [FieldOffset(724)] public Pointer<ColorScheme> Color;                              // 行 61
    [FieldOffset(728)] public Pointer<AnimTypeClass> Trailer;                          // 行 62
    [FieldOffset(732)] public int  ROT;                                                // 行 63
    [FieldOffset(736)] public int  CourseLockDuration;                                 // 行 64
    [FieldOffset(740)] public int  SpawnDelay;                                         // 行 65
    [FieldOffset(744)] public int  ScaledSpawnDelay;                                   // 行 66
    [FieldOffset(748)] public Bool Scalable;   // 行 67
    [FieldOffset(752)] public int  Arm;                                                // 行 69
    [FieldOffset(756)] public byte AnimLow;    // 行 70
    [FieldOffset(757)] public byte AnimHigh;   // 行 71
    [FieldOffset(758)] public byte AnimRate;   // 行 72
    [FieldOffset(759)] public Bool Flat;       // 行 73
}
```

> ⚠️⚠️ **`CreateBullet` 内部不判空**：`BulletTypeClass.cs:21-23` 是
> `ret.Ref.Target = Target;` —— 若底层 `0x46B050` 返回 0，这里立刻 `Ref` 空指针 → 崩。
> **调用前必须保证参数的 `WH` / `Owner` 非空，并且无法预判失败**。
> 对积木库来说，**不要**把这个 API 生成成"低风险"块。
>
> ⚠️ 第 1 个参数 `Target` **不传给引擎**（源码里被注释掉了 `/*Pointer<AbstractClass> @edx, */`，
> `BulletTypeClass.cs:19`），只用于返回值写回。要真正设目标请之后用 `pBullet.Ref.SetTarget(...)`
> 或 `pBullet.Ref.Target = ...`。
>
> ✅ 最典型的路径：`pWeapon.Ref.Projectile.Ref.CreateBullet(...)`。
> ⚠️ `pWeapon.Ref.Projectile` 可能为空（弹种没写 `Projectile=`），**也要判**。

### 4.4 `BulletClass`（`PatcherYRpp\BulletClass.cs:19-93`）

```csharp
[StructLayout(LayoutKind.Explicit, Size = 352)]
public struct BulletClass : IOwnAbstractType<BulletTypeClass>
{
    static public readonly IntPtr ArrayPointer;                                       // 0xA8ED40
    static public ref DynamicVectorClass<Pointer<BulletClass>> Array { get; }          // 行 23 ★

    // 行 29-32
    public void SetTarget(Pointer<AbstractClass> pTarget);      // 就是给 Target 字段赋值

    // 行 34-38
    public unsafe bool MoveTo(CoordStruct where, BulletVelocity velocity);             // ★ 虚 124

    // 行 40-44
    public unsafe void Fire(bool destroy = false);                                      // 0x468D80

    // 行 46-50
    public unsafe void Detonate(CoordStruct coords);                                    // 0x4690B0 ★

    // 行 52-56
    public void DetonateAndUnInit(CoordStruct coords);                                  // = Detonate + Base.UnInit()

    // 行 59-63
    public unsafe void LoseTarget();                                                    // 0x468430

    [FieldOffset(0)]   public ObjectClass Base;                                         // 行 65 ★（Health 等在这）
    [FieldOffset(172)] public Pointer<BulletTypeClass> Type;                            // 行 68
    [FieldOffset(176)] public Pointer<TechnoClass> Owner;                               // 行 69
    [FieldOffset(184)] public BulletData Data;                                          // 行 71
    [FieldOffset(224)] public Bool Bright;                                              // 行 72
    [FieldOffset(232)] public BulletVelocity Velocity;                                  // 行 74
    [FieldOffset(261)] public Bool CourseLocked;                                        // 行 76
    [FieldOffset(264)] public int CourseLockedDuration;                                 // 行 77
    [FieldOffset(268)] public Pointer<AbstractClass> Target;                            // 行 78 ★
    [FieldOffset(272)] public int Speed;                                                // 行 79
    [FieldOffset(276)] public int InheritedColor;                                       // 行 80
    [FieldOffset(296)] public Pointer<WarheadTypeClass> WH;                             // 行 82
    [FieldOffset(300)] public byte AnimFrame;                                           // 行 83
    [FieldOffset(301)] public byte AnimRateCounter;                                     // 行 84
    [FieldOffset(304)] public Pointer<WeaponTypeClass> WeaponType;                      // 行 85
    [FieldOffset(308)] public CoordStruct SourceCoords;                                 // 行 86
    [FieldOffset(320)] public CoordStruct TargetCoords;                                 // 行 87
    [FieldOffset(332)] public CellStruct LastMapCoords;                                 // 行 88
    [FieldOffset(336)] public int DamageMultiplier;                                     // 行 89
    [FieldOffset(340)] public Pointer<AnimClass> NextAnim;                               // 行 90
    [FieldOffset(344)] public Bool SpawnNextAnim;                                       // 行 91
    [FieldOffset(348)] public int Range;                                                // 行 92
}

// BulletClass.cs:10-17
public struct BulletData
{
    public TimerStruct UnknownTimer;   // 行 13
    public TimerStruct ArmTimer;       // 行 14
    public CoordStruct Location;       // 行 15
    public int Distance;               // 行 16
}
```

> ⚠️ `BulletClass` **没有 `Damage` 字段**（`Damage` 在 `WeaponTypeClass.Damage` /
> `CreateBullet` 的参数里；子弹上只有 `DamageMultiplier`）。

### 4.5 直接造成伤害的几种方式（**没有独立的 Damage helper 类**）

| 想做的事 | API | 出处 | 备注 |
| --- | --- | --- | --- |
| 对单个对象 | `pObj.Ref.TakeDamage(int damage, Pointer<WarheadTypeClass> pWH, bool crewed)` | `ObjectClass.cs:172` | 最简。⚠️ 见 §2.2 的悬垂指针说明 |
| 对单个对象（带凶手） | `pObj.Ref.TakeDamage(int damage, Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker, Pointer<HouseClass> pAttackingHouse, bool crewed)` | `ObjectClass.cs:177` | |
| 对单个对象（底层） | `pObj.Ref.ReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter, Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker, bool IgnoreDefenses, bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse)` | `ObjectClass.cs:183`（虚 91） | 返回值 `DamageState` |
| **范围伤害（推荐）** | `MapClass.DamageArea(CoordStruct Coords, int Damage, Pointer<TechnoClass> SourceObject, Pointer<WarheadTypeClass> WH, bool AffectsTiberium, Pointer<HouseClass> SourceHouse)` | `MapClass.cs:73-78` | `static`，返回 `DamageAreaResult`。源码注释说不能用 FastCall 委托（会 `StackOverflowException`），当前实现走 `ASM.FastCallTransferStation` |
| 算范围内总伤害 | `MapClass.GetTotalDamage(int Damage, Pointer<WarheadTypeClass> WH, Armor armor, int distance)` | `MapClass.cs:67-71` | `static` |
| 屏幕闪白（闪光弹） | `MapClass.FlashbangWarheadAt(int Damage, Pointer<WarheadTypeClass> WH, CoordStruct coords, bool Force = false, SpotlightFlags CLDisableFlags = SpotlightFlags.None)` | `MapClass.cs:80-84` | `static` |
| 造子弹再引爆 | `CreateBullet(...)` + `pBullet.Ref.Detonate(coords)` | §4.3 / §4.4 | 最接近"武器效果"，会走完整弹头逻辑 |

**`MapClass` 其它 public（`PatcherYRpp\MapClass.cs`）**

```csharp
[StructLayout(LayoutKind.Explicit, Size = 4468)]
public struct MapClass
{
    static public ref MapClass Instance { get; }                                  // 行 16
    public const int MaxCells = 0x40000;                                          // 行 18

    public bool TryGetCellAt(CellStruct MapCoords, out Pointer<CellClass> pCell);  // 行 20 ★
    public bool TryGetCellAt(CoordStruct Crd, out Pointer<CellClass> pCell);       // 行 32 ★（内部用 CellClass.Coord2Cell）
    public static int GetCellIndex(CellStruct MapCoords);                          // 行 37（= (Y<<9)+X）

    public static unsafe int GetTotalDamage(int Damage, Pointer<WarheadTypeClass> WH, Armor armor, int distance); // 行 67
    public static unsafe DamageAreaResult DamageArea(CoordStruct Coords, int Damage, Pointer<TechnoClass> SourceObject,
        Pointer<WarheadTypeClass> WH, bool AffectsTiberium, Pointer<HouseClass> SourceHouse);                     // 行 73
    public static unsafe void FlashbangWarheadAt(int Damage, Pointer<WarheadTypeClass> WH, CoordStruct coords,
        bool Force = false, SpotlightFlags CLDisableFlags = SpotlightFlags.None);                                 // 行 80

    public unsafe CellStruct PickCellOnEdge(Edge Edge, CellStruct CurrentLocation, CellStruct Fallback,
        SpeedType SpeedType, bool ValidateReachability, MovementZone MovZone);                                     // 行 86
    public unsafe CellStruct Pathfinding_Find(CellStruct position, SpeedType SpeedType, int a5,
        MovementZone MovementZone, bool alt, int SpaceSizeX, int SpaceSizeY, bool disallowOverlay, bool a11,
        bool requireBurrowable, bool allowBridge, CellStruct closeTo, bool a15, bool buildable);                   // 行 94

    [FieldOffset(312)]  public DynamicVectorClass<Pointer<CellClass>> Cells;      // 行 106
    [FieldOffset(4444)] public DynamicVectorClass<CellStruct> TaggedCells;        // 行 108
}
```

**❌ `MapClass` 里没有 `Coord2Cell`**。见 §6.4。

### 4.6 用武器直接开火

```csharp
// 让单位朝目标开火：不推荐在 OnFire 里调（递归），其它时候可以
Pointer<BulletClass> pB = pTechno.Ref.Fire(pTargetAbstract, 0);   // TechnoClass.cs:283
Pointer<BulletClass> pB2 = pTechno.Ref.Fire_NotVirtual(pTargetAbstract, 0); // 行 295
FireError err = pTechno.Ref.GetFireError(pTargetAbstract, 0, false);        // 行 268
```

---

## 5. 特效

### 5.1 激光：`YRMemory.Create<LaserDrawClass>(...)`（`PatcherYRpp\YRMemory.cs:94`）

```csharp
// YRMemory.cs:94-128
public static Pointer<T> Create<T>(params object[] list);
public static Pointer<T> Allocate<T>();                        // 行 48
public static unsafe IntPtr Allocate(uint size);               // 行 25
public static IntPtr AllocateChecked(uint size);               // 行 38
public static unsafe void Deallocate(IntPtr mem);              // 行 32
public static void Delete<T>(Pointer<T> ptr);                  // 行 132
```

**`LaserDrawClass`（`PatcherYRpp\LaserDrawClass.cs:11-43`）**

```csharp
[StructLayout(LayoutKind.Explicit, Size = 92)]
public struct LaserDrawClass
{
    // ★ 完整版（12 参数）
    public static unsafe void Constructor(Pointer<LaserDrawClass> pThis,
        CoordStruct source, CoordStruct target, int zAdjust, byte unknown,
        ColorStruct innerColor, ColorStruct outerColor, ColorStruct outerSpread,
        int duration, bool blinks = false, bool fades = true,
        float startIntensity = 1.0f, float endIntensity = 0.0f);            // 行 14-22

    // ★ 简版（7 参数，转发给完整版并补 0, 1）
    public static void Constructor(Pointer<LaserDrawClass> pThis,
        CoordStruct source, CoordStruct target,
        ColorStruct innerColor, ColorStruct outerColor, ColorStruct outerSpread,
        int duration);                                                     // 行 24-28

    public unsafe void Destructor();                                        // 行 30-34

    [FieldOffset(28)] public int Thickness;        // 只在 IsHouseColor 时起作用  行 36
    [FieldOffset(32)] public Bool IsHouseColor;                                  // 行 37
    [FieldOffset(33)] public Bool IsSupported;     // houseColor 下改 InnerColor 倍率 行 38
    [FieldOffset(65)] public ColorStruct InnerColor;                             // 行 40
    [FieldOffset(68)] public ColorStruct OuterColor;                             // 行 41
    [FieldOffset(71)] public ColorStruct OuterSpread;                            // 行 42
}
```

**`YRMemory.Create<LaserDrawClass>` 的实参表**（⚠️ `Create<T>` 用
`type.GetMethod("Constructor", paramTypes)` 反射匹配，`paramTypes` 由
`list[i].GetType()` 得出，`YRMemory.cs:108-117`）：

| # | 参数 | C# 实参该写的类型 |
| --- | --- | --- |
| 1 | `Pointer<LaserDrawClass> pThis` | 由 `Create` 自己填（`YRMemory.cs:108/114`） |
| 2 | `CoordStruct source` | `CoordStruct` |
| 3 | `CoordStruct target` | `CoordStruct` |
| 4 | `int zAdjust` | `int` |
| 5 | `byte unknown` | **`byte`** |
| 6 | `ColorStruct innerColor` | `ColorStruct` |
| 7 | `ColorStruct outerColor` | `ColorStruct` |
| 8 | `ColorStruct outerSpread` | `ColorStruct` |
| 9 | `int duration` | `int` |
| 10 | `bool blinks` | `bool` |
| 11 | `bool fades` | `bool` |
| 12 | `float startIntensity` | **`float`** |
| 13 | `float endIntensity` | **`float`** |

⚠️ 反射是**按精确类型**匹配 `Type.GetMethod(string, Type[])`：
* 传字面量 `(byte)1` 而不是 `1`（`1` 是 `int`，匹配不到完整版）；
* 传 `1.0f` 而不是 `1.0`（`double` 匹配不到）；
* 传 7 个参数时匹配简版（第 5 位 `byte` 不在表里，所以**简版必须严格 7 个实参**）。

**照抄可用**（与 `Karst\docs\已有功能0.1.0.md` 一致）：

```csharp
ColorStruct inner  = new ColorStruct(208, 10, 20);
ColorStruct outer  = new ColorStruct(88, 0, 20);
ColorStruct spread = new ColorStruct(10, 10, 10);

Pointer<LaserDrawClass> pLaser =
    YRMemory.Create<LaserDrawClass>(fromPos, toPos, inner, outer, spread, 50);  // 7 参 → 简版
if (pLaser.IsNotNull)                                    // ⚠️ 反射失败会抛异常（见下）
{
    pLaser.Ref.Thickness    = 6;
    pLaser.Ref.IsHouseColor = true;
}
```

⚠️⚠️ `Create<T>` 的失败模式是**抛异常**：
`MethodInfo constructor = type.GetMethod("Constructor", paramTypes);`（`YRMemory.cs:117`）
为 `null` 时，`constructor.Invoke` 抛 `NullReferenceException`（被 catch 后
`Deallocate(ptr)` 再 rethrow，行 123-127）。
**积木库生成代码时，`YRMemory.Create<T>` 应外层 try/catch 或至少让用户知道会抛。**

⚠️ `Create<LaserDrawClass>` **不返回"画出来的激光对象"**——它是引擎堆上的一块内存，
由引擎自己绘制/回收。没有"销毁激光"的公开入口（只有 `Destructor()`，通常别调）。
❓ 未确认：`YRMemory.Delete<LaserDrawClass>` 是否安全（会调 `Destructor` 再 `Deallocate`）。

### 5.2 动画：`AnimClass` / `AnimTypeClass`

**`AnimTypeClass`（`PatcherYRpp\AnimTypeClass.cs:11-97`）**

```csharp
[StructLayout(LayoutKind.Explicit, Size = 888)]
public struct AnimTypeClass
{
    static public readonly IntPtr ArrayPointer;                                    // 0x8B4150
    static public YRPP.GLOBAL_DVC_ARRAY<AnimTypeClass> ABSTRACTTYPE_ARRAY;          // 行 15 ★

    [FieldOffset(0)]   public ObjectTypeClass Base;                                 // 行 17
    [FieldOffset(660)] public int ArrayIndex;                                       // 行 19
    [FieldOffset(680)] public double Damage;                                        // 行 25
    [FieldOffset(688)] public int Rate;                                             // 行 26
    [FieldOffset(692)] public int Start;                                            // 行 27
    [FieldOffset(696)] public int LoopStart;                                        // 行 28
    [FieldOffset(700)] public int LoopEnd;                                          // 行 29
    [FieldOffset(704)] public int End;                                              // 行 30
    [FieldOffset(708)] public int LoopCount;                                        // 行 31
    public Pointer<AnimTypeClass> Next { get; set; }                                // 行 33
    [FieldOffset(716)] public int SpawnsParticle;                                   // 行 34
    [FieldOffset(720)] public int NumParticles;                                     // 行 35
    [FieldOffset(724)] public int DetailLevel;                                      // 行 36
    [FieldOffset(732)] public RandomStruct RandomLoopDelay;                          // 行 38
    [FieldOffset(740)] public RandomStruct RandomRate;                              // 行 39
    [FieldOffset(748)] public int Translucency;                                     // 行 40
    public Pointer<AnimTypeClass> Spawns { get; set; }                              // 行 42
    [FieldOffset(756)] public int SpawnCount;                                       // 行 43
    [FieldOffset(760)] public int Report;      // VocClass index                  // 行 44
    [FieldOffset(764)] public int StopSound;   // VocClass index                  // 行 45
    public Pointer<AnimTypeClass> BounceAnim { get; set; }                           // 行 47
    public Pointer<AnimTypeClass> ExpireAnim { get; set; }                           // 行 49
    public Pointer<AnimTypeClass> TrailerAnim { get; set; }                          // 行 51
    [FieldOffset(780)] public int TrailerSeperation;                                // 行 52
    [FieldOffset(784)] public double Elasticity;                                    // 行 53
    [FieldOffset(792)] public double MinZVel;                                       // 行 54
    [FieldOffset(808)] public double MaxXYVel;                                      // 行 56
    public Pointer<WarheadTypeClass> Warhead { get; set; }                           // 行 58
    [FieldOffset(820)] public int DamageRadius;                                     // 行 59
    public Pointer<OverlayTypeClass> TiberiumSpawnType { get; set; }                 // 行 61
    [FieldOffset(828)] public int TiberiumSpreadRadius;                             // 行 62
    [FieldOffset(832)] public int YSortAdjust;                                      // 行 63
    [FieldOffset(836)] public int YDrawOffset;                                      // 行 64
    [FieldOffset(840)] public int ZAdjust;                                          // 行 65
    [FieldOffset(844)] public int MakeInfantry;                                     // 行 66
    [FieldOffset(848)] public int RunningFrames;                                    // 行 67
    [FieldOffset(852)] public bool IsFlamingGuy;                                    // 行 68
    [FieldOffset(855)] public bool TiberiumChainReaction;                           // 行 71
    [FieldOffset(857)] public bool HideIfNoOre;                                     // 行 73
    [FieldOffset(858)] public bool Bouncer;                                         // 行 74
    [FieldOffset(863)] public bool FreeLoad;                                        // 行 79
    [FieldOffset(865)] public bool AltPalette;                                      // 行 81
    [FieldOffset(868)] public Layer Layer;                                          // 行 83
    [FieldOffset(874)] public bool Translucent;                                     // 行 86
    [FieldOffset(875)] public bool Scorch;                                          // 行 87
    [FieldOffset(876)] public bool Flamer;                                          // 行 88
    // …（其余 bool 字段 77-96 行）
}
```

**`AnimClass`（`PatcherYRpp\AnimClass.cs:11-99`）**

```csharp
[StructLayout(LayoutKind.Explicit, Size = 456)]
public struct AnimClass : IOwnAbstractType<AnimTypeClass>
{
    static public readonly IntPtr ArrayPointer;                                     // 0xA8E9A8
    static public ref DynamicVectorClass<Pointer<AnimClass>> Array { get; }          // 行 14 ★

    public unsafe void SetOwnerObject(Pointer<ObjectClass> pOwner);                  // 行 20-24
    public void Pause();                                                            // 行 26-31
    public void Unpause();                                                          // 行 33-37

    // ★ 两个 Constructor（YRMemory.Create<AnimClass>(...) 会反射调用它们）
    public static void Constructor(Pointer<AnimClass> pThis, Pointer<AnimTypeClass> pAnimType, CoordStruct Location); // 行 39-42
    public static unsafe void Constructor(Pointer<AnimClass> pThis, Pointer<AnimTypeClass> pAnimType, CoordStruct Location,
        int LoopDelay = 0, int LoopCount = 1, uint flags = 0x600, int ForceZAdjust = 0, bool reverse = false);     // 行 43-49

    [FieldOffset(0)]   public ObjectClass Base;                                      // 行 51-52
    [FieldOffset(172)] public ProgressTimer Animation;                               // 行 55
    [FieldOffset(200)] public Pointer<AnimTypeClass> Type;                           // 行 56
    [FieldOffset(204)] public Pointer<ObjectClass> OwnerObject;                      // 行 57
    [FieldOffset(216)] public int LightConvertIndex;                                 // 行 60
    public AnsiStringPointer PaletteName { get; }                                    // 行 62
    [FieldOffset(252)] public int TintColor;                                         // 行 63
    [FieldOffset(256)] public int ZAdjust;                                           // 行 64
    [FieldOffset(260)] public int YSortAdjust;                                       // 行 65
    [FieldOffset(264)] public CoordStruct FlamingGuyCoords;                          // 行 66
    [FieldOffset(276)] public int FlamingGuyRetries;                                 // 行 67
    [FieldOffset(280)] public Bool IsBuildingAnim;                                   // 行 68
    [FieldOffset(281)] public Bool UnderTemporal;                                    // 行 69
    [FieldOffset(282)] public Bool Paused;                                           // 行 70
    [FieldOffset(283)] public Bool Unpaused;                                         // 行 71
    [FieldOffset(284)] public int PausedAnimFrame;                                   // 行 72
    [FieldOffset(288)] public Bool Reverse;                                          // 行 73
    [FieldOffset(296)] public BounceClass Bounce;                                    // 行 75
    [FieldOffset(376)] public byte TranslucencyLevel;                                // 行 76
    [FieldOffset(377)] public Bool TimeToDie;                                        // 行 77
    public Pointer<BulletClass> AttachedBullet { get; set; }                          // 行 79
    [FieldOffset(384)] public Pointer<HouseClass> Owner;                             // 行 81
    [FieldOffset(388)] public int LoopDelay;                                         // 行 82
    [FieldOffset(392)] public double Damage;                                         // 行 83
    [FieldOffset(400)] public BlitterFlags AnimFlags;                                // 行 84
    [FieldOffset(404)] public Bool HasExtras;                                        // 行 85
    [FieldOffset(405)] public byte RemainingIterations;                              // 行 86
    [FieldOffset(408)] public Bool IsPlaying;                                        // 行 89
    [FieldOffset(409)] public Bool IsFogged;                                         // 行 90
    [FieldOffset(410)] public Bool FlamingGuyExpire;                                 // 行 91
    [FieldOffset(411)] public Bool UnableToContinue;                                  // 行 92
    [FieldOffset(412)] public Bool SkipProcessOnce;                                  // 行 93
    [FieldOffset(413)] public Bool Invisible;                                        // 行 94
    [FieldOffset(414)] public Bool PowerOff;                                         // 行 95
}
```

**创建动画的写法**

```csharp
Pointer<AnimTypeClass> pAnimType = AnimTypeClass.ABSTRACTTYPE_ARRAY.Find("EXPLOLRG");  // ← 只有这一个查找入口
if (pAnimType.IsNull) return;

// 方式 A（3 参 → 短 Constructor）
Pointer<AnimClass> pAnim = YRMemory.Create<AnimClass>(pAnimType, coords);

// 方式 B（想控制 LoopDelay / LoopCount / flags，用完整 Constructor 的 8 参）
Pointer<AnimClass> pAnim2 = YRMemory.Create<AnimClass>(
    pAnimType, coords, 0, 1, 0x600u, 0, false);   // ⚠️ uint 字面量要写 0x600u；bool 写 false
```

❌ **`AnimClass` 上没有 `Create` 静态方法**（全仓搜 `AnimClass Create` = 0 命中）。
❌ **`AnimTypeExt.ExtMap` 对脚本没有意义**：`AnimTypeExt` 的 CTOR/DTOR/LoadFromINI 三个 Hook
**全部被注释掉了**（`Ext\AnimTypeExt.cs:32/41/50`），所以这个容器**永远是空的**——
`AnimTypeExt.ExtMap.Find(...)` 必然返回 `null`。

❓ **未确认**：`YRMemory.Create<AnimClass>(...)` 之后动画是否会**自动**加入
`AnimClass.Array` 并被引擎绘制。按 `Constructor` 直接调 `0x421EA0`（原生 ctor）来看，
引擎的其它注册路径（Put / 加入 Array）**可能没有走**。参考项目里常见的做法是
`YRMemory.Create<AnimClass>(...)` 后什么都不做，但我**没有在本仓源码或已验证示例里找到证据**。
建议积木库把这个块标成"待实测"。

### 5.3 音效 / 语音 ❌

* ❌ **没有 `VocClass`**：全 `PatcherYRpp\*.cs` 里搜 `VocClass` 只命中
  `AnimTypeClass.cs:44`、`:45` 的**注释**（"VocClass index"）。
* ❌ **没有 `VoxClass`**。
* ❌ **没有 `PlaySound` / `PlayEVA` 之类的封装**（搜 `PlaySound|PlayEVA|VocClass|VoxClass` 全部 0 命中）。
* ⚠️ 唯一沾边的是**字段级**：
  * `AnimTypeClass.Report` / `.StopSound`（`AnimTypeClass.cs:44-45`，`int`，VocClass 索引）——只能在 INI 里写。
  * `WeaponTypeClass.Report` / `.DownReport`（`WeaponTypeClass.cs:29-30`，`DynamicVectorClass<int>`）。
* ❓ **未确认**：`Game.CurrentSWType`（`Unsorted.cs:24`）等是否能触发引擎音效；未验证。

> **结论：当前这套绑定里，用户脚本无法播放音效。** 积木库不要生成"播放音效"块。

### 5.4 其它特效相关

```csharp
// CellClass（PatcherYRpp\CellClass.cs:20-264）—— 与"格子"有关的渲染/内容
public unsafe Pointer<TechnoClass>  FindTechnoNearestTo(Point2D offsetPixel, bool alt, Pointer<TechnoClass> pExcludeThis); // 行 23
public unsafe Pointer<ObjectClass>  FindObjectOfType(AbstractType abs, bool alt);   // 行 29
public unsafe Pointer<BuildingClass> GetBuilding();                                 // 行 35
public unsafe Pointer<UnitClass>     GetUnit(bool alt);                             // 行 41
public unsafe Pointer<InfantryClass> GetInfantry(bool alt);                         // 行 47
public unsafe Pointer<AircraftClass> GetAircraft(bool alt);                         // 行 53
public unsafe Pointer<TerrainClass>  GetTerrain(bool alt);                          // 行 59
public unsafe Pointer<ObjectClass>   GetSomeObject(CoordStruct coords, bool alt);   // 行 69
public unsafe Pointer<CellClass>     GetNeighbourCell(Direction direction);         // 行 99
public unsafe bool  CanPutTiberium();                                              // 行 93
public unsafe bool  IncreaseTiberium(int idxTiberium, int amount);                  // 行 124
public unsafe bool  ReduceTiberium(int amount);                                     // 行 129
public unsafe int   GetContainedTiberiumIndex();                                    // 行 112
public unsafe int   GetContainedTiberiumValue();                                    // 行 117
public unsafe int   GetFloorHeight(Point2D subCoords);                              // 行 135
public unsafe CoordStruct GetCenterCoords();     // ★ 格子中心，行 141
public bool  ContainsBridge();                                                     // 行 149
public Pointer<ObjectClass> GetContent();                                          // 行 154
public int   GetLevel();                                                           // 行 159
public static CoordStruct Cell2Coord(CellStruct cell, int z = 0);                   // 行 164 ★
public static CellStruct   Coord2Cell(CoordStruct crd);                             // 行 168 ★★
public CoordStruct FixHeight(CoordStruct crd);                                     // 行 173
public CoordStruct GetCoordsWithBridge();                                          // 行 182
public unsafe void MarkForRedraw();                                                // 行 187
public unsafe void AddContent(Pointer<ObjectClass> pContent, bool onBridge);        // 行 203
public unsafe void RemoveContent(Pointer<ObjectClass> pContent, bool onBridge);     // 行 208
// 字段：MapCoords(36) BridgeOwnerCell(44) IsoTileTypeIndex(56) OverlayTypeIndex(68) SmudgeTypeIndex(72)
//       WallOwnerIndex(80) InfantryOwnerIndex(84) CloakByHouses(120) Jumpjet(224) FirstObject(228)
//       AltObject(232) LandType(236) RadLevel(240) Height(282) Level(283) SlopeIndex(284) Powerup(286)
//       Shroudedness(288) Foggedness(289) BlockedNeighbours(290) OccupationFlags(292) Flags(320)
```

`Drawing.cs` / `Surface.cs` / `Blitters.cs` / `TacticalClass.cs`：画东西用的底层绑定，
属于"画面绘制"能力，本清单**未逐一展开**（不在脚本常见需求内；需要时另做）。

---

## 6. 超武

### 6.1 `HouseClass.FindSuperWeapon`（`PatcherYRpp\HouseClass.cs:32-44`）

```csharp
public Pointer<SuperClass> FindSuperWeapon(Pointer<SuperWeaponTypeClass> pType);   // 行 32
// 内部：遍历 this.Supers（DynamicVectorClass<Pointer<SuperClass>>，行 244），
//       比对 pItem.Ref.Type == pType，找不到返回 Pointer<SuperClass>.Zero
```

### 6.2 `SuperClass`（`PatcherYRpp\SuperClass.cs:10-172`）

```csharp
[StructLayout(LayoutKind.Explicit, Size = 128)]
public struct SuperClass : IOwnAbstractType<SuperWeaponTypeClass>
{
    static public readonly IntPtr ArrayPointer;                                      // 0xA83CB8
    static public ref DynamicVectorClass<Pointer<SuperClass>> Array { get; }          // 行 17
    static public ref DynamicVectorClass<Pointer<SuperClass>> ShowTimers { get; }     // 行 20（⚠️ 源码复用了 ArrayPointer，疑似上游 bug）

    public unsafe void Reset();                                                       // 行 24（0x6CE0B0）
    public unsafe bool SetOnHold(bool onHold);                                        // 行 30（0x6CB4D0）
    public unsafe bool Grant(bool oneTime, bool announce, bool onHold);               // 行 36（0x6CB560）
    public unsafe bool Lose();                                                        // 行 43（0x6CB7B0）
    public bool IsPowered();                                                          // 行 49（读 Type.Ref.IsPowered）
    public unsafe void Launch(CellStruct cell, bool isPlayer);                        // 行 54 ★（0x6CC390）
    public unsafe bool CanFire();                                                     // 行 60（0x6CC360）
    public unsafe void SetReadiness(bool ready);                                      // 行 66（0x6CB820）
    public unsafe byte StopPreclickAnim(bool isPlayer);                               // 行 72（0x6CB830）
    public unsafe byte ClickFire(bool isPlayer, CellStruct cell);                     // 行 78（0x6CB920）
    public unsafe bool HasChargeProgressed(bool isPlayer);                             // 行 86（0x6CBCA0）
    public unsafe int  GetCameoChargeState();                                         // 行 92（0x6CBEE0）
    public unsafe void SetCharge(int percentage);                                     // 行 98 ★（0x6CC1E0）
    public unsafe int  GetRechargeTime();                                             // 行 104（0x6CC260）
    public unsafe void SetRechargeTime(int time);                                     // 行 110（0x6CC280）
    public unsafe void ResetRechargeTime();                                           // 行 116（0x6CC290）
    public unsafe string NameReadiness();                                             // 行 122（0x6CC2B0）
    public unsafe bool ShouldDrawProgress();                                          // 行 128（0x6CDE90）
    public unsafe bool ShouldFlashTab();                                              // 行 134（0x6CE1A0）

    [FieldOffset(0)]   public AbstractClass Base;                                     // 行 141
    [FieldOffset(36)]  public int CustomChargeTime;                                   // 行 143
    [FieldOffset(40)]  public Pointer<SuperWeaponTypeClass> Type;                     // 行 144 ★
    public Pointer<HouseClass> Owner { get; set; }                                    // 行 145-146（偏移 44）
    [FieldOffset(48)]  public TimerStruct RechargeTimer;                              // 行 148
    [FieldOffset(60)]  public UniStringPointer UIName;                                // 行 150
    [FieldOffset(64)]  public Bool BlinkState;                                        // 行 151
    [FieldOffset(72)]  public long BlinkTimer;                                        // 行 153
    [FieldOffset(80)]  public int SpecialSoundDuration;                               // 行 154
    [FieldOffset(84)]  public CoordStruct SpecialSoundLocation;                       // 行 155
    [FieldOffset(96)]  public Bool CanHold;                                           // 行 156
    [FieldOffset(98)]  public CellStruct ChronoMapCoords;                             // 行 158
    public Pointer<AnimClass> Animation { get; set; }                                 // 行 161（偏移 104）
    [FieldOffset(108)] public Bool AnimationGotInvalid;                               // 行 163
    [FieldOffset(109)] public Bool Granted;                                           // 行 164
    [FieldOffset(110)] public Bool OneTime;                                           // 行 165
    [FieldOffset(111)] public Bool IsCharged;     // ★ 行 166
    [FieldOffset(112)] public Bool IsOnHold;                                          // 行 167
    [FieldOffset(116)] public int  ReadinessFrame;                                    // 行 169
    [FieldOffset(120)] public int  CameoChargeState;                                  // 行 170
    [FieldOffset(124)] public ChargeDrainState ChargeDrainState;                      // 行 171
}
```

> ✅ `IsCharged` 是**字段**（`Bool`，偏移 111），不是属性。可读可写。
> ⚠️ 标准"立刻放超武"的写法（照抄 `Karst\docs\已有功能0.1.0.md`，但**注意把
> `MapClass.Coord2Cell` 换成 `CellClass.Coord2Cell`**）：
>
> ```csharp
> Pointer<SuperWeaponTypeClass> pSWType = SuperWeaponTypeClass.ABSTRACTTYPE_ARRAY.Find("IronCurtain");
> if (pSWType.IsNull) return;
> Pointer<HouseClass> pHouse = Owner.OwnerObject.Ref.Owner;      // TechnoClass.Owner，行 439
> if (pHouse.IsNull) return;
> Pointer<SuperClass> pSuper = pHouse.Ref.FindSuperWeapon(pSWType);
> if (pSuper.IsNull) return;
>
> CellStruct cell = CellClass.Coord2Cell(Owner.OwnerObject.Ref.Base.Base.GetCoords());  // ★ CellClass，不是 MapClass
> pSuper.Ref.IsCharged = true;
> pSuper.Ref.Launch(cell, true);
> pSuper.Ref.IsCharged = false;
> ```
> ⚠️ `Launch` 之后立刻把 `IsCharged` 设回 `false` 是 **Karst 文档的写法**，
> 但语义上等于"放完不消耗"，❓ 未确认是否应该保留/由引擎自己清。

### 6.3 `SuperWeaponTypeClass`（`PatcherYRpp\SuperWeaponTypeClass.cs:11-47`）

```csharp
[StructLayout(LayoutKind.Explicit, Size = 256)]
public struct SuperWeaponTypeClass
{
    static public readonly IntPtr ArrayPointer;                                      // 0xA8E330
    static public YRPP.GLOBAL_DVC_ARRAY<SuperWeaponTypeClass> ABSTRACTTYPE_ARRAY;     // 行 16 ★

    [FieldOffset(0)]   public AbstractTypeClass Base;                                 // 行 18
    [FieldOffset(152)] public int ArrayIndex;                                         // 行 20
    [FieldOffset(156)] public Pointer<WeaponTypeClass> WeaponType;                    // 行 21
    [FieldOffset(176)] public int RechargeTime;  // in frames                          // 行 26
    [FieldOffset(180)] public SuperWeaponType Type;                                   // 行 27
    [FieldOffset(184)] public Pointer<SHPStruct> SidebarImage;                        // 行 28
    [FieldOffset(188)] public int Action;                                             // 行 29
    [FieldOffset(192)] public int SpecialSound;                                       // 行 30
    [FieldOffset(196)] public int StartSound;                                         // 行 31
    [FieldOffset(200)] public Pointer<BuildingTypeClass> AuxBuilding;                 // 行 32
    public AnsiStringPointer SidebarImageFile { get; }                                // 行 34
    [FieldOffset(229)] public Bool UseChargeDrain;                                    // 行 35
    [FieldOffset(230)] public Bool IsPowered;                                         // 行 36
    [FieldOffset(231)] public Bool DisableableFromShell;                              // 行 37
    [FieldOffset(232)] public int  FlashSidebarTabFrames;                             // 行 38
    [FieldOffset(236)] public Bool AIDefendAgainst;                                   // 行 39
    [FieldOffset(237)] public Bool PreClick;                                          // 行 40
    [FieldOffset(238)] public Bool PostClick;                                         // 行 41
    [FieldOffset(240)] public int  PreDependent;                                      // 行 42
    [FieldOffset(244)] public Bool ShowTimer;                                         // 行 43
    [FieldOffset(245)] public Bool ManualControl;                                     // 行 44
    [FieldOffset(248)] public float Range;                                            // 行 45
    [FieldOffset(252)] public int  LineMultiplier;                                    // 行 46
}
```

### 6.4 ★ `MapClass.Coord2Cell` 核实结论：**不存在**

* `MapClass` 的**全部** public 成员见 §4.5 —— 里面**没有** `Coord2Cell`。
* **正确写法是 `CellClass.Coord2Cell`**，`static`，签名：
  ```csharp
  // PatcherYRpp\CellClass.cs:168-171
  public static CellStruct Coord2Cell(CoordStruct crd)
  {
      return new CellStruct(crd.X / Game.CellSize, crd.Y / Game.CellSize);
  }
  ```
  反向：`public static CoordStruct Cell2Coord(CellStruct cell, int z = 0)`（`CellClass.cs:164-167`）。
* 另一个更安全的替代：`MapClass.Instance.TryGetCellAt(CoordStruct Crd, out Pointer<CellClass> pCell)`
  （`MapClass.cs:32-36`，内部就是用 `CellClass.Coord2Cell`，并且**带范围检查**，
  越界时返回 `false` 而不是给你一个野指针）。
* `Karst\docs\已有功能0.1.0.md` 里写的 `MapClass.Coord2Cell(...)` 是**错的**（正是
  `refs\karstpatcher\README.md:476` 与 早期 HANDOFF 文档提到的旧样本 API）。
  建议顺手修文档（本次任务只允许新增 md，故只在这里记录）。

⚠️ `MapClass.GetCellIndex(CellStruct)`（`MapClass.cs:37-40`）**不做范围检查**；
`MapClass.Cells` 是 `DynamicVectorClass<Pointer<CellClass>>`（偏移 312），
索引前应自己判 `0 <= idx < MaxCells`（`MaxCells = 0x40000`）。

---

## 7. 数学与随机

### 7.1 `MathEx`（`PatcherYRpp\Helpers\MathEx.cs`）—— **全部 public 成员只有 3 个**

```csharp
namespace PatcherYRpp.Utilities
{
    public static class MathEx
    {
        public static Random Random { get; } = new Random();       // 行 31 ★ 类型是 System.Random
        public static int    Clamp(int value, int min, int max);    // 行 33
        public static double Clamp(double value, double min, double max); // 行 38
    }
}
```

* ❌ **没有 `Lerp`**、❌ **没有 `Distance`**、❌ **没有 `GetDistance`**、❌ 没有 `Deg2Rad` 等。
* `MathEx.Random` 是 `System.Random`，所以可用：
  `Next()` / `Next(int maxValue)` / `Next(int minValue, int maxValue)` /
  `NextDouble()` / `NextBytes(byte[])` / `NextInt64()`（.NET 4.8 有）。
  **也可以自己再 `new Random()`**（不推荐：同 tick 会拿到相同种子）。
* 角度/距离请用 `System.Math`（`Math.Sqrt` / `Math.Cos` / `Math.Atan2` …）
  + `CoordStruct.Magnitude()` / `DistanceFrom()`（§7.2）。

⚠️ 这个 `MathEx` 是 **Karst 自己写的补丁文件**（文件头注释说明：原
`PatcherYRpp.Utilities` 项目不在参考仓里，上游只用到 `MathEx.Random` 一个成员）。
所以**不要**按"上游 MathEx 有很多方法"的旧印象生成代码。

### 7.2 `CoordStruct`（`PatcherYRpp\BasicStructures.cs:31-106`）

```csharp
[DebuggerDisplay("XYZ={X}, {Y}, {Z}")]
[StructLayout(LayoutKind.Sequential)]
[Serializable]
public struct CoordStruct
{
    public CoordStruct(int x, int y, int z);                  // 行 33

    public static CoordStruct operator -(CoordStruct a);           // 行 40 ★ 一元负
    public static CoordStruct operator +(CoordStruct a, CoordStruct b); // 行 44 ★
    public static CoordStruct operator -(CoordStruct a, CoordStruct b); // 行 51 ★
    public static CoordStruct operator *(CoordStruct a, double r);      // 行 58 ★
    public static double      operator *(CoordStruct a, CoordStruct b); // 行 66 ★ 点积
    public static bool operator ==(CoordStruct a, CoordStruct b);       // 行 89
    public static bool operator !=(CoordStruct a, CoordStruct b);       // 行 93

    public double Magnitude();                                 // 行 73
    public double MagnitudeSquared();                           // 行 78
    public double DistanceFrom(CoordStruct other);              // 行 84 ★
    public override bool Equals(object obj);                    // 行 95
    public override int GetHashCode();                          // 行 96
    public override string ToString();                          // 行 98  → "(x, y, z)"

    public int X;   // 行 103
    public int Y;   // 行 104
    public int Z;   // 行 105
}
```

❌ **没有 `operator/`**（要除就自己写 `a * (1.0 / d)`），❌ 没有 `Length`、没有 `Normalized()`。

### 7.3 `CellStruct`（`BasicStructures.cs:183-250`）

```csharp
[StructLayout(LayoutKind.Sequential)]
[Serializable]
public struct CellStruct
{
    public CellStruct(int x, int y);                                    // 行 188
    public static CellStruct operator -(CellStruct a);                   // 行 194
    public static CellStruct operator +(CellStruct a, CellStruct b);     // 行 198
    public static CellStruct operator -(CellStruct a, CellStruct b);     // 行 204
    public static CellStruct operator *(CellStruct a, double r);         // 行 210
    public static double     operator *(CellStruct a, CellStruct b);     // 行 217
    public static bool operator ==(CellStruct a, CellStruct b);          // 行 239
    public static bool operator !=(CellStruct a, CellStruct b);          // 行 243

    public double Magnitude();                                          // 行 223
    public double MagnitudeSquared();                                   // 行 228
    public double DistanceFrom(CellStruct other);                       // 行 234

    public short X;   // 行 248 ★ 是 short，不是 int
    public short Y;   // 行 249
}
```

⚠️ `CellStruct.X/Y` 是 **`short`**。`new CellStruct(int, int)` 构造后内部会被截成 `short`。

### 7.4 其它结构（`BasicStructures.cs`）

| 类型 | 行 | 字段/说明 |
| --- | --- | --- |
| `ColorStruct` | 14-26 | `public ColorStruct(int r, int g, int b)`（行 16，会 `(byte)` 截断）；`byte R, G, B`（行 23-25） |
| `BulletVelocity` | 111-181 | `BulletVelocity(double x, double y, double z)`（行 113）；`double X, Y, Z`（行 178-180）；`+ - * Magnitude DistanceFrom ==` |
| `RandomStruct` | 255-258 | `public int Min, Max;` |
| `Point2D` | 365-429 | `Point2D(int x, int y)`；`int X, Y`；`+ - * Magnitude DistanceFrom` |
| `RectangleStruct` | 434-501 | `RectangleStruct(int x, int y, int z, int w)`；`X, Y, Width, Height`；**唯一有 `operator/` 的结构**（行 472） |
| `SingleVector3D` | 264-339 | `float X, Y, Z` |
| `TintStruct` | 513-518 | `int Red, Green, Blue` |
| `BytePalette` | 504-509 | `ColorStruct Entries_first`；`Pointer<ColorStruct> Entries` |
| `Matrix3D` | 521-537 | `float _00 … _23` |

### 7.5 朝向 / 朝向运算（`PatcherYRpp\GeneralStructures.cs`）

```csharp
// DirStruct 行 168-309
public struct DirStruct
{
    public DirStruct(int value);                                     // 行 170
    public DirStruct(double rad);                                    // 行 175
    public DirStruct(uint bits, short value);                        // 行 179
    public short GetValue(uint Bits = 16);                           // 行 183
    public void  SetValue(short value, uint Bits = 16);              // 行 196
    public static uint TranslateFixedPoint(uint bitsFrom, uint bitsTo, uint value, uint offset = 0); // 行 208
    public short value8();  public void value8(short value);          // 行 229 / 234
    public short value32(); public void value32(short value);         // 行 239 / 244
    public short value256();public void value256(short value);        // 行 249 / 254
    public short value();   public void value(short value);           // 行 259 / 264
    public double radians(uint Bits = 16);                            // 行 269 ★
    public void   radians(double rad, uint Bits = 16);                // 行 284 ★
    public static bool operator ==(DirStruct a, DirStruct b);          // 行 299
    public static bool operator !=(DirStruct a, DirStruct b);          // 行 303
    public short Value;                                               // 行 307
}

// FacingStruct 行 313-412
public struct FacingStruct
{
    public short turn_rate();                                        // 行 321
    public void  turn_rate(short value);                              // 行 326
    public bool  in_motion();                                         // 行 336
    public DirStruct target();                                        // 行 341 ★
    public DirStruct current();                                       // 行 346 ★（含插值）
    public bool  set(DirStruct value);                                // 行 365
    public bool  turn(DirStruct value);                               // 行 380
    public short difference();                                        // 行 398
    public int   num_steps();                                         // 行 403
    public DirStruct Value;    // 目标朝向                             // 行 408
    public DirStruct Initial;                                         // 行 409
    public TimerStruct Timer;                                         // 行 410
    public DirStruct ROT;                                             // 行 411
}
```

**读朝向**：`Owner.OwnerObject.Ref.Facing.current().radians()` → `double` 弧度；
写朝向：`Owner.OwnerObject.Ref.Facing.set(new DirStruct(rad));`。

**计时器**（`GeneralStructures.cs`）：

```csharp
public struct TimerStruct                            // 行 12-90
{
    public TimerStruct(int duration);                // 行 14
    public void Start(int duration);                 // 行 15
    public void Stop();                              // 行 21
    public void Pause();                             // 行 27
    public void Resume();                            // 行 37
    public int  GetTimeLeft();                       // 行 44
    public bool Completed();                         // 行 58
    public bool InProgress();                        // 行 66
    public bool Expired();                           // 行 72
    public int StartTime, gap, TimeLeft;             // 行 87-89
}
public struct RepeatableTimerStruct                   // 行 94-114
{
    public RepeatableTimerStruct(int duration);      // 行 96
    public void Start(int duration);                 // 行 101
    public void Restart();                           // 行 107
    public TimerStruct Base; public int Duration;    // 行 112-113
}
public struct ProgressTimer                           // 行 119-164
{
    public ProgressTimer(int duration);              // 行 121
    public void Start(int duration);                 // 行 130
    public void Start(int duration, int step);       // 行 135
    public bool Update();                            // 行 142
    public int Value; public Bool HasChanged; public RepeatableTimerStruct Timer; public int Step; // 160-163
}
```

### 7.6 `Game`（`PatcherYRpp\Unsorted.cs:9-32`）

```csharp
public static class Game
{
    public static int CurrentFrame { get; set; }      // 行 11 ★ 帧号（做计时/间隔）
    public const int LevelHeight  = 104;              // 行 15
    public const int BridgeLevels = 4;                // 行 16
    public const int BridgeHeight = 416;              // 行 17
    public const int CellSize     = 256;              // 行 19 ★（CoordStruct ↔ CellStruct 换算）
    public static int IKnowWhatImDoing { get; set; }  // 行 21
    public static int CurrentSWType { get; set; }     // 行 24
    public static unsafe bool HasDirtyArea();         // 行 27
}
```

### 7.7 枚举（`GeneralDefinitions.cs`）—— 积木库做下拉框用

| 枚举 | 行 | 值 |
| --- | --- | --- |
| `AbstractFlags` | 10-16 | `None/Techno/Object/Foot`（`[Flags]`） |
| `Armor` | 18-31 | `None, Flak, Plate, Light, Medium, Heavy, Wood, Steel, Concrete, Special_1, Special_2` |
| `AbstractType` | 33-109 | `None, Unit, Aircraft, AircraftType, Anim, AnimType, Building, BuildingType, Bullet, BulletType, Campaign, Cell, Factory, House, HouseType, Infantry, InfantryType, Isotile, IsotileType, BuildingLight, Overlay, OverlayType, Particle, ParticleType, ParticleSystem, ParticleSystemType, Script, ScriptType, Side, Smudge, SmudgeType, Special, SuperWeaponType, TaskForce, Team, TeamType, Terrain, TerrainType, Trigger, TriggerType, UnitType, VoxelAnim, VoxelAnimType, Wave, Tag, TagType, Tiberium, Action, Event, WeaponType, WarheadType, Waypoint, Abstract, Tube, LightSource, EMPulse, TacticalMap, Super, AITrigger, AITriggerType, Neuron, FoggedObject, AlphaShape, VeinholeMonster, NavyType, SpawnManager, CaptureManager, Parasite, Bomb, RadSite, Temporal, Airstrike, SlaveManager, DiskLaser` |
| `DamageState` | 111-119 | `Unaffected, Unchanged, NowYellow, NowRed, NowDead, PostMortem` |
| `DamageAreaResult` | 121-126 | `Hit, Missed, Nullified` |
| `Direction` | 135-153 | `N/NE/E/SE/S/SW/W/NW`（+ `North…` 别名） |
| `SpotlightFlags` | 155-162 | `[Flags] None, NoColor, NoRed, NoGreen, NoBlue` |
| `LandType` | 163-177 | `Clear, Road, Water, Rock, Wall, Tiberium, Beach, Rough, Ice, Railroad, Tunnel, Weeds` |
| `Layer` | 179-187 | `None=-1, Underground, Surface, Ground, Air, Top` |
| `PlacementType` | 189-195 | `Remove, Put, Redraw, AddContent` |
| `ChargeDrainState` | 197-203 | `None=-1, Charging, Ready, Draining` |
| `SuperWeaponType` | 205-220 | `Invalid=-1, Nuke, IronCurtain, LightningStorm, ChronoSphere, ChronoWarp, ParaDrop, AmerParaDrop, PsychicDominator, SpyPlane, GeneticMutator, ForceShield, PsychicReveal` |
| `BlitterFlags` | 222-246 | `[Flags]`（`Plain=0x20, Centered=0x200, Alpha=0x800, Flat=0x2000, ZRead=0x3000, ZReadWrite=0x4000, …`） |
| `VisualType` | 248-256 | `Normal, Indistinct, Darken, Shadowy, Ripple, Hidden` |
| `Move` | 258-268 | `OK, Cloak, MovingBlock, ClosedGate, FriendlyDestroyable, Destroyable, Temp, No` |
| `ZGradient` | 270-277 | `None=-1, Ground, Deg45, Deg90, Deg135` |
| `FireError` | 280-295 | `NONE=-1, OK, AMMO, FACING, REARM, ROTATING, ILLEGAL, CANT, MOVING, RANGE, CLOAKED, BUSY, MUST_DEPLOY` |
| `Rank` | 296-302 | `Invalid=-1, Elite=0, Veteran=1, Rookie=2` |
| `SpeedType` | 304-315 | `None=-1, Foot, Track, Wheel, Hover, Winged, Float, Amphibious, FloatBeach` |
| `MovementZone` | 317-333 | `None=-1, Normal, Crusher, Destroyer, AmphibiousDestroyer, AmphibiousCrusher, Amphibious, Subterrannean, Infantry, InfantryDestroyer, Fly, Water, WaterBeach, CrusherAll` |
| `Mission` | 335-370 | `None=-1, Sleep, Attack, Move, QMove, Retreat, Guard, Sticky, Enter, Capture, Eaten, Harvest, Area_Guard, Return, Stop, Ambush, Hunt, Unload, Sabotage, Construction, Selling, Repair, Rescue, Missile, Harmless, Open, Patrol, ParadropApproach, ParadropOverfly, Wait, AttackMove, SpyplaneApproach, SpyplaneOverfly` |
| `TargetFlags` | 372-393 | `[Flags]`（`Air=0x4, Infantry=0x8, Vehicles=0x10, Buildings=0x20, …`） |
| `RadioCommand` | 395-437 | 36 个命令 |
| `NetworkEvents` | 439-488 | 事件类型表 |
| `Edge` | 490-498 | `None=-1, North, East, South, West, Air` |
| `GameMode` | 500-508 | `Campaign, Modem, NullModem, LAN, Internet, Skirmish` |
| `CellFlags`（在 `CellClass.cs:10-18`） | 10-18 | `[Flags] None, IsWaypoint=0x4, Explored=0x8, Bridge=0x100` |

---

## 8. `Logger`（`karstpatcher\Logger.cs`）—— ✅ 全部核实

```csharp
namespace KarstPatcher
{
    public class Logger
    {
        public delegate void WriteLineDelegate(string str);                      // 行 14
        static public WriteLineDelegate WriteLine { get; set; }                  // 行 16

        // ★ 支持 {0} 格式串（内部 string.Format，行 20）
        static public void Log(string format, params object[] args);             // 行 18
        static public void Log(string str);                                      // 行 26
        static public void Log(object obj);                                      // 行 32（obj.ToString()）

        public static bool HasException { get; set; } = false;                    // 行 38
        public static void PrintException(Exception e);                           // 行 41（递归打印 InnerException）

        public static void LogWithColor(string str, ConsoleColor color);          // 行 65
        public static void LogWithColor(string str, ConsoleColor fgColor,
                                        ConsoleColor bgColor);                    // 行 71

        static public void LogError(string format, params object[] args);        // 行 88
        static public void LogError(string str);                                 // 行 96（加 "[Error] " 前缀 + 红字）
        static public void LogError(object obj);                                 // 行 102

        static public void LogWarning(string format, params object[] args);      // 行 108
        static public void LogWarning(string str);                               // 行 116（加 "[Warning] " + 黄字）
        static public void LogWarning(object obj);                               // 行 122
    }
}
```

* ✅ 全部是 **`static`**，无需实例。
* ✅ `{0}` 格式串**支持**（`string.Format`）。
* ⚠️ **重载歧义**：`Logger.Log("abc")` 会命中 `Log(string str)`；
  `Logger.Log(obj)`（`object`）当 `obj` 是 `string` 时**优先选 `Log(string)`**。
  传字符串变量时行为确定，但别指望 `Log(object)` 的 `ToString()` 语义。
* ⚠️ `Log(string, params object[])` 里 `string.Format` 的格式错误（如 `"{1}"` 只有一个参数）
  会抛 `FormatException`。脚本层有 try/catch（`ScriptManager.SafeScriptCall`，`ScriptManager.cs:363-373`），
  不会崩游戏，但会**静默**只留下一条异常日志（且该帧该脚本后续逻辑被跳过）。
* ⚠️ `WriteLine` 是委托属性，**框架启动时才会被赋值**；如果在框架初始化前调用 `Log`
  会是 `NullReferenceException`（`Logger.cs:28` 的 `WriteLine.Invoke`）。脚本运行时不会遇到。

---

## 9. Decorators / Components / INIReader

### 9.1 `Component`（`Extension\Components\Component.cs`）—— 脚本的祖先

```csharp
[Serializable]
public abstract class Component : IHaveComponent, IReloadable
{
    public const int NO_ID = -1;                                  // 行 15
    public Component();                                           // 行 16
    public Component(bool withoutTransform = false);              // 行 22
    public Component(int id);                                     // 行 28

    public int    ID { get; internal set; }                       // 行 33 ⚠️ internal set
    public string Name { get; set; }                              // 行 35 ★ 可写
    public string Tag  { get; set; }                              // 行 36 ★ 可写（给脚本当便签）
    public Component Parent { get; }                              // 行 38
    public Component Root { get; }                                // 行 39
    public Transform Transform { get; }                           // 行 44 ⚠️ 注释写"finish 前别用"
    public Component AttachedComponent { get; }                   // 行 46（= this）
    public Component AttachedComponentIfAwaked { get; }           // 行 49
    public bool IsAwaked { get; }                                 // 行 408

    public Component GetRoot();                                   // 行 51
    public void AttachToComponent(Component component);           // 行 59 ★
    public void DetachFromParent();                               // 行 67

    // 直系子节点
    public Component   GetComponent(Predicate<Component> predicate);  // 行 79
    public Component   GetComponent(int id);                          // 行 84
    public Component   GetComponent(Type type);                       // 行 89
    public TComponent  GetComponent<TComponent>() where TComponent : Component;   // 行 94 ★
    public Component[] GetComponents(Func<Component, bool> predicate);            // 行 104
    public Component[] GetComponents();                                          // 行 109
    public Component[] GetComponents(Type type);                                 // 行 114
    public TComponent[] GetComponents<TComponent>() where TComponent : Component;// 行 118 ★

    // 递归所有子节点
    public Component  GetComponentInChildren(Predicate<Component> predicate);      // 行 131
    public Component  GetComponentInChildren(int id);                              // 行 151
    public Component  GetComponentInChildren(Type type);                           // 行 156
    public TComponent GetComponentInChildren<TComponent>() where TComponent : Component; // 行 161 ★
    public Component[] GetComponentsInChildren(Func<Component, bool> predicate);   // 行 171
    public Component[] GetComponentsInChildren();                                  // 行 183
    public Component[] GetComponentsInChildren(Type type);                         // 行 188
    public TComponent[] GetComponentsInChildren<TComponent>() where TComponent : Component; // 行 192 ★

    // 子 + 父
    public Component  GetComponentInParent(Predicate<Component> predicate);        // 行 203
    public Component  GetComponentInParent(int id);                                // 行 218
    public Component  GetComponentInParent(Type type);                             // 行 223
    public TComponent GetComponentInParent<TComponent>() where TComponent : Component; // 行 228 ★
    public Component[] GetComponentsInParent(Func<Component, bool> predicate);     // 行 238
    public Component[] GetComponentsInParent();                                    // 行 247
    public Component[] GetComponentsInParent(Type type);                           // 行 252
    public TComponent[] GetComponentsInParent<TComponent>() where TComponent : Component; // 行 256

    protected virtual void AddComponent(Component component);                       // 行 263
    protected virtual void RemoveComponent(Component component);                    // 行 267

    // 生命周期（virtual）
    public virtual void Awake();                                                    // 行 277
    public virtual void Start();                                                    // 行 283
    public virtual void OnUpdate();                                                 // 行 286 ★
    public virtual void OnRender();                                                 // 行 289 ❌ 无调用点
    public virtual void OnDestroy();                                                // 行 293 ⚠️ 只在 Component.Destroy 里调
    public virtual void SaveToStream(IStream stream);                               // 行 298
    public virtual void LoadFromStream(IStream stream);                             // 行 302

    public void Foreach(Action<Component> action);                                  // 行 339
    public void ForeachChild(Action<Component> action);                             // 行 343
    public static void ForeachComponents(IEnumerable<Component> components, Action<Component> action); // 行 348
    public static void ForeachComponents(Component root, Action<Component> action);   // 行 360（含 root）
    public static void Destroy(Component component);                                  // 行 370
    public void EnsureAwaked();                                                      // 行 376
    public void EnsureStarted();                                                     // 行 385
}
```

**`ExtComponent<TExt>`（`Components\ExtComponent.cs:13-79`）**

```csharp
[Serializable]
public class ExtComponent<TExt> : Component where TExt : class, IExtension
{
    public ExtComponent(TExt owner, int id, string name);      // 行 15
    public TExt Owner { get; }                                  // 行 24
    public event Action OnAwake;                                // 行 25
    public override void Awake();                               // 行 27
    public override void OnUpdate();                            // 行 33
    public ExtComponent<TExt> GetAwaked();                      // 行 56 ⚠️ 有副作用（会唤醒）
    public void CreateScriptComponent<T>(int id, string description, params object[] parameters)
        where T : Scriptable<TExt>;                             // 行 63
    public void CreateScriptComponent<T>(string description, params object[] parameters)
        where T : Scriptable<TExt>;                             // 行 72
}
```

**`IHaveComponent`（`Components\IHaveComponent.cs`）**

```csharp
public interface IHaveComponent
{
    Component AttachedComponent { get; }
    Component AttachedComponentIfAwaked { get; }   // Karst 新增
}
```

**`ComponentManager`（`Components\ComponentManager.cs`，13 行）**：❓ 内容未细看，不是脚本 API。

**`Transform`（`Components\Transform.cs`，59 行）**：❓ 未细看；`Component.Transform` 注释说
"don't use before finish"（`Component.cs:41-44`）。

### 9.2 `EventDecorator`（`Extension\Decorators\EventDecorator.cs`）

```csharp
namespace Extension.Decorators
{
    [Serializable]
    public abstract class EventDecorator : Decorator
    {
        public virtual void OnUpdate() { }        // 行 33 —— 每帧（和脚本同一时机）
        public virtual void OnRemove() { }        // 行 39 —— ⚠️ 见下
    }

    // Decorator.cs:11-16
    [Serializable]
    public abstract class Decorator
    {
        public string Description { get; set; }         // 行 13
        public DecoratorId ID { get; set; }             // 行 14
        public IDecorative Decorative { get; internal set; } // 行 15 ⚠️ internal set
    }
}
```

**怎么用**（三步，全部已核实）：

```csharp
// 1) 定义（必须有无参构造：DecoratorMap 用 Activator.CreateInstance(typeof(T), parameters)，DecoratorMap.cs:32）
public class MyDecorator : EventDecorator
{
    private int _tick;
    public override void OnUpdate()
    {
        _tick++;
        // 这里可以访问宿主？⚠️ 不行 —— 见下
    }
    public override void OnRemove() { }
}

// 2) 挂到单位上（Owner 是 TechnoExt）
DecoratorId id = new DecoratorId(12345);                       // DecoratorId.cs:17
MyDecorator d = Owner.DecoratorComponent.CreateDecorator<MyDecorator>(id, "my state");
// 或者让组件自己随机一个 id：
MyDecorator d2 = Owner.DecoratorComponent.CreateDecorator<MyDecorator>("my state");

// 3) 取 / 删
MyDecorator got = Owner.DecoratorComponent.Get<MyDecorator>(id);   // DecoratorComponent.cs:40
Owner.DecoratorComponent.Remove(id);                               // 行 33
Owner.DecoratorComponent.Remove(d);                                // 行 35
```

**`DecoratorComponent` 全部 public（`Decorators\DecoratorComponent.cs`）**

```csharp
[Serializable]
public class DecoratorComponent : Component, IDecorative, IDecorative<PairDecorator>
{
    public DecoratorComponent();                                                     // 行 14（base(true) → 无 Transform）

    public TDecorator CreateDecorator<TDecorator>(DecoratorId id, string description,
        params object[] parameters) where TDecorator : Decorator;                     // 行 19 ★
    public TDecorator CreateDecorator<TDecorator>(string description,
        params object[] parameters) where TDecorator : Decorator;                     // 行 25 ★

    public Decorator Get(DecoratorId id);                                            // 行 31
    public void Remove(DecoratorId id);                                              // 行 33
    public void Remove(Decorator decorator);                                         // 行 35
    public IEnumerable<PairDecorator> GetPairDecorators();                            // 行 38
    public TDecorator Get<TDecorator>(DecoratorId id) where TDecorator : Decorator;  // 行 40 ★

    // "键值对"式装饰器（给对象挂自定义状态）
    public object GetValue(DecoratorId id);                                          // 行 45
    public void   SetValue(DecoratorId id, object value);                             // 行 49
    public TValue GetValue<TValue>(DecoratorId id);                                   // 行 53
    public TValue GetValue<TKey, TValue>(DecoratorId id);                             // 行 57
    public void   SetValue<TKey, TValue>(DecoratorId id, TValue value);               // 行 61
    public PairDecorator GetPairDecorator(object key);                                // 行 66
    public PairDecorator<TKey, TValue> GetPairDecorator<TKey, TValue>(TKey key);       // 行 78
    public object GetValue(object key);                                              // 行 91
    public void   SetValue(object key, object value);                                 // 行 96
    public TValue GetValue<TValue>(object key);                                       // 行 105
    public TValue GetValue<TKey, TValue>(TKey key);                                   // 行 110
    public void   SetValue<TKey, TValue>(TKey key, TValue value);                      // 行 115

    public void DriveDecorators();                                                    // 行 132（Karst 新增，框架调用）
    public void NotifyDecoratorsRemoved();                                            // 行 154（Karst 新增，❌ 无调用点）
}
```

> ⚠️⚠️ **`EventDecorator.OnRemove()` 永远不会被调用**：
> `NotifyDecoratorsRemoved()`（`DecoratorComponent.cs:154`）在全仓**没有任何调用点**
> （grep `NotifyDecoratorsRemoved` 只命中定义那一行）。所以 `OnRemove` 只能作为"接口预留"，
> **不要**把释放逻辑放进去。
>
> ⚠️ **`EventDecorator` 拿不到它挂在谁身上**：`Decorator` 只有 `Description` / `ID` / `Decorative`
> （`Decorator.cs:13-15`），而 `Decorative` 是 `internal set`，**外部读可以**（`Decorative` 是 public 属性）
> 但它类型是 `IDecorative`，**没有** `OwnerObject`。想要"装饰器能访问单位"，
> 只能在构造时自己把需要的指针/`ExtensionReference` 通过 `parameters` 传进去。
>
> ⚠️ **`CreateDecorator` 用 `Activator.CreateInstance(typeof(T), parameters)`**（`DecoratorMap.cs:32`）：
> `parameters` 的类型必须能匹配到你写的构造函数，否则抛 `MissingMethodException`。
> 无参构造是最安全的。
>
> ⚠️ **`DecoratorId` 的相等性只看那个 `int`**（`DecoratorId.cs:29-32`），
> `SearchTip`（`None/Pair/Event`，行 9-12）**不参与比较**。同一 id 重复 `Add` 会抛
> `ArgumentException`（`DecoratorMap.Add` → `Dictionary.Add`，行 51）。
>
> ⚠️ **`DecoratorComponent` 本身是 `Component`，但没覆写 `OnUpdate`**——
> `Component.Foreach` 走到它时不会驱动装饰器。真正驱动的是
> `ScriptManager.DriveExtensions` 里显式调的 `DriveDecorators()`（`ScriptManager.cs:438-444`），
> 且**只对 `TechnoExt` / `BulletExt`**。

### 9.3 `PairDecorator`（`Decorator.cs:19-50`）

```csharp
[Serializable]
public abstract class PairDecorator : Decorator
{
    public object Key { get; set; }        // 行 21
    public object Value { get; set; }      // 行 22
    public PairDecorator(object key, object val);   // 行 23
    protected abstract object GetKey();  protected abstract object SetKey(object key);
    protected abstract object GetValue();protected abstract object SetValue(object value);
}

[Serializable]
public class PairDecorator<TKey, TValue> : PairDecorator
{
    public new TKey Key { get; set; }       // 行 38
    public new TValue Value { get; set; }   // 行 39
    public PairDecorator(TKey key, TValue val);  // 行 40
}
```

### 9.4 `ExtensionReference<TExt>`（`Extension\Utilities\ExtensionReference.cs`）★ 保存"别的对象"

```csharp
[Serializable]
public struct ExtensionReference<TExt> : ISerializable where TExt : class, IExtension
{
    public ExtensionReference(TExt ext);                 // 行 43
    public ExtensionReference(IntPtr ptr);                // 行 48
    public void Set(TExt ext);                            // 行 52
    public void Set(IntPtr ptr);                          // 行 64
    public TExt Get();                                    // 行 69  ★ 失效时返回 null
    public bool TryGet(out TExt result);                  // 行 74  ★ 更安全的写法
    public bool ReferenceCollected { get; }               // 行 107
    public void GetObjectData(SerializationInfo info, StreamingContext context);  // 行 21

    public static implicit operator TExt(ExtensionReference<TExt> reference);      // 行 116
    public static implicit operator ExtensionReference<TExt>(TExt ext);            // 行 117
}
```

标准用法（照抄 `Karst\scripts\examples\cs\MissileRain.cs:34-55`）：

```csharp
private ExtensionReference<TechnoExt> _target = new ExtensionReference<TechnoExt>();

// 存
if (pTarget.CastToTechno(out Pointer<TechnoClass> pTechno))
    _target.Set(TechnoExt.ExtMap.Find(pTechno));

// 取（推荐 TryGet，因为 Get() 内部就是 TryGet 然后可能给 null）
TechnoExt target = _target.Get();
if (target == null) return;                                 // ★ 必须先判
CoordStruct pos = target.OwnerObject.Ref.Base.Base.GetCoords();
```

> ⚠️ **不要保存裸 `Pointer<T>` 跨帧**：对象被销毁后指针就悬空了。
> `ExtensionReference<TExt>` 内部用 `WeakReference` + swizzle（行 16-17），是唯一的安全方案。
> ⚠️ `TryGet` 判的是 `result.OwnerObject != IntPtr.Zero`（行 78）——
> 判断是"扩展还活着"，不是"引擎对象还活着"。**仍然建议再判 `IsNull`/`IsAlive`**。

### 9.5 `INIReader` / `INI_EX` / `Parser<T>`（`Extension\Utilities\INIReader.cs`、`INIParser.cs`）

```csharp
// INIReader.cs:11-79
public class INIReader
{
    public INIReader(INI_EX exINI);                          // 行 15
    public INIReader(Pointer<CCINIClass> pINI);              // 行 20
    public bool ReadNormal<T>(string section, string key, ref T buffer);       // 行 45 ★
    public bool ReadArray<T>(string section, string key, ref T[] buffer);      // 行 49
    public bool ReadList<T>(string section, string key, ref List<T> buffer);   // 行 53
    public bool ReadSuperWeapon(string section, string key, ref Pointer<SuperWeaponTypeClass> buffer); // 行 58
    public bool ReadScripts(string section, string key, ref List<Script.Script> buffer);              // 行 69
}

// INIParser.cs:209-294
public class INI_EX
{
    public INI_EX(Pointer<CCINIClass> pIniFile);              // 行 214
    public byte[] value();                                    // 行 219
    public int max_size();                                    // 行 224
    public bool empty();                                      // 行 229
    public int  ReadString(string section, string key);        // 行 235
    public bool Read<T>(string section, string key, ref T pBuffer, int Count = 1);     // 行 240
    public bool Read<T>(string section, string key, ref T[] pBuffer, int Count = 1);   // 行 248
    public bool ReadBool(string section, string key, ref bool bBuffer);                // 行 257
    public bool ReadInteger(string section, string key, ref int nBuffer);              // 行 262
    public bool ReadDouble(string section, string key, ref double nBuffer);            // 行 267
    public bool ReadArray<T>(string section, string key, ref T[] aBuffer);             // 行 272
    public bool ReadList<T>(string section, string key, ref List<T> list);             // 行 277
}

// INIParser.cs:10-207 —— 底层解析（支持 string/bool/int/uint/sbyte/byte/long/ulong/short/ushort/float/double）
public class Parser<T>
{
    public static int Parse(byte[] buffer, ref T outValue);         // 行 20
    public static int Parse(byte[] buffer, ref T[] outValue);       // 行 26
    public static int Parse(string str, ref T outValue);            // 行 32
    public static int Parse(string str, ref T[] outValue);          // 行 46
}
```

**INIReader 在框架里的实际用法**（`Ext\TechnoTypeExt.cs:29-36`）：

```csharp
protected override void LoadFromINIFile(Pointer<CCINIClass> pINI)
{
    INI_EX exINI = new INI_EX(pINI);
    INIReader reader = new INIReader(exINI);
    string section = OwnerObject.Ref.Base.Base.ID;     // 类型的 ID 就是 INI 段名
    reader.ReadScripts(section, "Scripts", ref Scripts);
}
```

> ⚠️⚠️ **脚本侧目前拿不到 `INIReader`**：
> * `TechnoTypeExt`（`Ext\TechnoTypeExt.cs`）**没有**暴露 `INI_EX` / `INIReader` / 任意自定义键的
>   getter（public 成员只有 `ExtMap` / `Scripts` / `OwnerObject`，见 §1.3）。
> * 用户脚本里**没有** `Pointer<CCINIClass>` 的现成入口（`CCINIClass.INI_Rules` 是全局 rules，
>   但那是 rulesmd.ini，不是你的自定义 INI），也没有 `CCFileClass` 读取自定义文件的封装入口
>   （`CCFileClass.cs` 49 行，❓ 未细看是否能构造出可用对象）。
> * **结论**：`INIReader` 是"给 Ext 层写扩展用的"，**不是给用户脚本用的 API**。
>   积木库目前**不要**生成"读自定义 INI 键"块。若需要，得先在 `TechnoTypeExt` 上加一个
>   public 的 `INI_EX` 缓存（那是改源码，不在本任务范围）。
>
> ✅ 唯一"脚本能配置自己"的现成通道就是 `rulesmd.ini` 的 `[单位ID] Scripts=类名`。

### 9.6 `ContainerHelper`（`Extension\Utilities\ContainerHelper.cs`）

```csharp
public static class ContainerHelper
{
    public static uint WriteObject(this IStream stream, object obj);                     // 行 17
    public static uint ReadObject<T>(this IStream stream, out T obj) where T : class;     // 行 37
    public static void Swizzle<TBase, T>(this Extension<TBase> ext, ref T obj);           // 行 62
    public static void Here_I_Am<TBase, T>(this Extension<TBase> ext, Pointer<T> oldPtr, ref T obj); // 行 66
    public static void Here_I_Am<TBase, T>(this Extension<TBase> ext, IStream stream, ref T obj);    // 行 70
    public static void Save<TBase, T>(this Extension<TBase> ext, IStream stream, PointerHandle<T> ptr); // 行 77
    public static void Load<TBase, T>(this Extension<TBase> ext, IStream stream, ref PointerHandle<T> ptr); // 行 81
    public static IContainer GetContainer<TExt>();                                       // 行 92
}
```

### 9.7 `ScriptManager`（`Extension\Script\ScriptManager.cs`）—— 面向脚本作者的部分

```csharp
public partial class ScriptManager
{
    public static Script GetScript(string scriptName);                      // 行 27
    public static List<Script> GetScripts(string fileName);                 // 行 61
    public static List<Script> GetScripts(List<string> scriptList);         // 行 83
    public static void CreateScriptableTo<T>(Component root, IEnumerable<Script> scripts, T owner);  // 行 102
    public static Scriptable<T> CreateScriptableTo<T>(Component root, Script script, T owner);       // 行 112
    public static TScriptable CreateScriptable<TScriptable>(Script script, params object[] parameters)
        where TScriptable : ScriptComponent;                                // 行 121
    public static Scriptable<T> CreateScriptable<T>(Script script, T owner); // 行 131

    // 驱动入口（框架调用，脚本一般不直接调）
    public static void OnPutScripts(TechnoExt ext);                          // 行 266
    public static void OnReceiveDamageScripts(TechnoExt ext, Pointer<int> pDamage, int distanceFromEpicenter,
        Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker, bool ignoreDefenses,
        bool preventPassengerEscape, Pointer<HouseClass> pAttackingHouse);   // 行 295
    public static void OnRemoveScripts(TechnoExt ext);                       // 行 311
    public static void OnDetonateScripts(BulletExt ext, Pointer<CoordStruct> pCoords); // 行 323
    public static void OnFireScripts(TechnoExt ext, Pointer<AbstractClass> pTarget, int weaponIndex); // 行 378
}
```

**`Script`（`Script\Script.cs:11-20`）**

```csharp
[Serializable]
public class Script
{
    public Script(string name);                    // 行 13
    public string Name { get; protected set; }      // 行 18
    public Type ScriptableType { get; internal set; } // 行 19
}
```

> ⚠️ 脚本类名（`Scripts=` 的值）与 `Type.Name` **必须完全一致**（`ScriptManager.FindScriptAssembly`，
> 行 145-161，用的是 `type.Name == scriptName`）。积木库生成类名时不要加命名空间。

---

## 10. ❌ 明确不存在 / 有坑清单

### 10.1 源码里**不存在**的旧版 API（生成器必须屏蔽）

| 旧写法（❌ 不要生成） | 现状 | 正确写法 | 依据 |
| --- | --- | --- | --- |
| `TechnoExt.Get(pTechno)` | ❌ 不存在 | `TechnoExt.ExtMap.Find(pTechno)`（返回 `TechnoExt` 或 `null`） | `TechnoExt.cs:21`（只有 `ExtMap` 字段 + `Find`），全仓 grep `TechnoExt.Get` 0 命中 |
| `TechnoExt.CreateDecorator(...)` | ❌ 不存在 | `Owner.DecoratorComponent.CreateDecorator<T>(...)` | `TechnoExt.cs` 无此成员；`DecoratorComponent.cs:19` |
| `MapClass.Coord2Cell(...)` | ❌ 不存在 | `CellClass.Coord2Cell(crd)`（`static`），或 `MapClass.Instance.TryGetCellAt(crd, out pCell)` | `MapClass.cs` 全部 public 见 §4.5；`CellClass.cs:168`；`MapClass.cs:32` |
| `Pointer<T>.Raw` | ❌ 不存在 | `(int)ptr` / `(long)ptr` / public 字段 `ptr.Value` | `Pointer.cs` 全部成员见 §3.1 |
| `CellSpreadEnumerator` | ❌ 不存在 | 自己按 `WarheadTypeClass.CellSpread`（float）+ `CellClass.Coord2Cell` 双层循环 | grep 0 命中；`WarheadTypeClass.cs:20` |
| `EventDecorator` 的**旧构造** | ❌ 不存在（旧样本里可能是 `EventDecorator(id, desc, ...)` 之类） | 继承 `EventDecorator` 写**无参构造**，用 `CreateDecorator<T>(id, desc)` 挂 | `EventDecorator.cs:30-40`（抽象类，无构造声明 → 只能默认无参） |
| `VocClass.Play(...)` / `VoxClass` | ❌ 不存在 | **没有替代品**（见 §5.3） | grep 0 命中 |
| `AnimClass.Create(...)` | ❌ 不存在 | `YRMemory.Create<AnimClass>(pAnimType, coords)`（❓ 待实测） | grep 0 命中；`AnimClass.cs:39-49` |
| `MapClass.Coord2Cell` 的"类方法"形式 `MapClass.Instance.Coord2Cell(...)` | ❌ 不存在 | 同上 | 同第一行 |

> `refs\karstpatcher\README.md:473-481` 记录了旧样本（`GGIScript.cs` / `MTNKScript.cs`）
> 因为用了这些 API 被移出 `KarstPatcher`；`Karst\docs\已有功能0.1.0.md` 也说明了这一点。
> **但它们没说明"正确写法是什么"**——本清单的右列就是那个缺失的答案。

### 10.2 容易被误用导致崩溃 / 静默失效的点

| # | 坑 | 具体表现 | 正确做法 | 依据 |
| --- | --- | --- | --- | --- |
| 1 | **不判空就 `.Ref`** | `Pointer<T>.Ref` 是 `Unsafe.AsRef<T>(Value)`，空指针 → `AccessViolationException`，**不可 catch**，游戏直接退 | 每个 `Ref` 前 `if (!p.IsNull)` / `if (p.IsNotNull)`；`ABSTRACTTYPE_ARRAY.Find` 找不到返回 `Zero` | `Pointer.cs:40-44`；`YRPP.cs:25-34` |
| 2 | **`CreateBullet` 内部不判返回** | `ret.Ref.Target = Target;`（`BulletTypeClass.cs:22`）在底层返回 0 时立刻崩 | 无法预防；尽量用已验证的 `WeaponTypeClass` + 非空 `WH`；不要把它做成"一键伤害"块 | `BulletTypeClass.cs:17-24` |
| 3 | **`TakeDamage(int,...)` 的悬垂 `int*`** | `ReceiveDamage(Pointer<int>.AsPointer(ref damage), ...)` 把局部变量地址传进引擎（`ObjectClass.cs:179`） | 用堆上/字段级的 `int` 持有伤害值，再用 `Pointer<int>.AsPointer(ref field)` | `ObjectClass.cs:172-180` |
| 4 | **构造/析构期读字段** | 在 `OnAwake` / `OnRemove` 里读 `Type`、`OwnerObject.Ref.*`：对象可能还没建成/已在拆 | `OnRemove` 里不要再读引擎字段；组件访问用 `AttachedComponentIfAwaked`（`TechnoExt.cs:44`） | `TechnoExt.cs:43-54`、`163-199`；`ScriptManager.cs:341-344` |
| 5 | **`AttachedComponent` / `ExtComponent` 会唤醒组件** | `get_AttachedComponent` → `GetAwaked()` → `EnsureAwaked()` → 跑 `OnAwake`，在析构期会踩死对象，并把 hook 搞 `UnHook` | 需要"可能未唤醒"的场景用 `AttachedComponentIfAwaked`（返回 `null` 而非唤醒） | `ExtComponent.cs:56-61`；`TechnoExt.cs:39/44`；`README.md:522-539` |
| 6 | **`OnFire` 里再 `Fire`** | `OnFire` 由 `TechnoClass::Fire_NotVirtual`（`0x6FDD50`）派发，再次 `Fire` 会无限递归 | 用 `SetTarget` + `QueueMission(Mission.Attack, true)`，或置标志、下一帧再开火 | `TechnoExt.cs:144-157` |
| 7 | **`AnimTypeExt.ExtMap` 是死容器** | 三个 Hook 全被注释（`AnimTypeExt.cs:32/41/50`），容器永远空 → `Find` 恒返回 `null` | 动画类型只用 `AnimTypeClass.ABSTRACTTYPE_ARRAY.Find` | `AnimTypeExt.cs:32,41,50` |
| 8 | **`EventDecorator.OnRemove` 不会被调** | `NotifyDecoratorsRemoved()` 无调用点 | 清理逻辑放到别处（如脚本的 `OnRemove`） | `DecoratorComponent.cs:154`；grep 0 调用点 |
| 9 | **`OnRender` / `OnDestroy` 对脚本无效** | 无调用点（`OnDestroy` 只在手动 `Component.Destroy` 里跑） | 不要生成这两个回调的骨架 | `Component.cs:289,293,370`；grep 0 调用点 |
| 10 | **`OnPut` 的 `faceDir` 是 `default`** | `ScriptManager.cs:271-278` 明确留默认值（`Direction.N`） | 朝向要从 `Ref.Facing.current()` 读 | `ScriptManager.cs:271-278` |
| 11 | **`BulletScriptable.OnPut` 永不触发** | `DriveExtensions` 只对 `TechnoExt` 调 `OnPutScripts` | 子弹脚本只用 `OnDetonate` | `ScriptManager.cs:423-427` |
| 12 | **`YRMemory.Create<T>` 反射失败会抛** | `GetMethod(...)` 返回 `null` → `NullReferenceException`（`YRMemory.cs:117-127`） | 实参类型必须精确：`byte`/`float` 别写成 `int`/`double`；外层可考虑 try/catch | `YRMemory.cs:94-128` |
| 13 | **`IsAlive` / `InLimbo` 是 `Bool` 不是 `bool`** | `if (obj.IsAlive)` 编译不过（没有到 bool 的隐式转换？——实际上 `Bool` **有** `implicit operator bool`，所以 `if` 能用；但 `obj.IsAlive == true`、`?.`、模式匹配会有摩擦） | 显式 `if (obj.IsAlive.ToBoolean())` 最稳；`Bool` 也有 `==`/`!=` 重载 | `Bool.cs:32-52` |
| 14 | **`CellStruct.X/Y` 是 `short`** | 大坐标/计算溢出 | 计算用 `int`，最后构造 `new CellStruct(x, y)` | `BasicStructures.cs:248-249` |
| 15 | **`WeaponTypeClass.Range` 是 ×256** | 拿它当"格"用会差 256 倍 | 除以 `256`（或 `Game.CellSize`） | `WeaponTypeClass.cs:27` |
| 16 | **`Logger.Log` 格式串写错抛 `FormatException`** | 该脚本帧后续被跳过（异常被 `SafeScriptCall` 吃掉） | 格式串参数个数对齐；或用 `Logger.Log(str)` | `Logger.cs:18-23`；`ScriptManager.cs:363-373` |
| 17 | **脚本异常被静默吃掉** | `SafeScriptCall` 只 `PrintException`，不中断也不上报给用户 | 调试期多看 `karstpatcher.log` | `ScriptManager.cs:355-373` |
| 18 | **保存裸 `Pointer<T>` 跨帧** | 对象销毁后悬空 | 用 `ExtensionReference<TExt>`（§9.4） | `ExtensionReference.cs` |
| 19 | **`DecoratorId` 重复 `Add` 抛异常** | `Dictionary.Add` | 先 `Get(id)` 判断，或让组件随机 id | `DecoratorMap.cs:50-54` |
| 20 | **`TechnoClass.Type` 是属性，子类 `Type` 是字段** | 混用会编译错 | 按 §2.1/§2.6 的表 | `TechnoClass.cs:191` vs `UnitClass.cs:21` |
| 21 | **`TechnoClass.MindControlledByHouse` 类型是 `Pointer<AnimClass>`** | 按名字会以为能拿 `HouseClass` | 要房子请用 `Ref.Owner` 或 `AbstractClass.GetOwningHouse()` | `TechnoClass.cs:465`、`AbstractClass.cs:47` |
| 22 | **`SuperClass.ShowTimers` 疑似上游 bug** | 它的 `ArrayPointer` 与 `Array` 用的是同一个地址（`SuperClass.cs:17/20`） | 别用 `ShowTimers` | `SuperClass.cs:17,20` |

### 10.3 ❓ 未确认（我不敢打包票，积木库先别做成"保证可用"块）

1. `YRMemory.Create<AnimClass>(pAnimType, coords)` 之后动画**是否会自动进 `AnimClass.Array` / 被绘制**。
   （Constructor 直调 `0x421EA0`，引擎的注册路径可能没走全。）
2. `YRMemory.Delete<LaserDrawClass>(ptr)` 是否会重复释放 / 调 `Destructor` 后 `Deallocate` 是否配套。
3. `SuperClass.Launch` 之后 `IsCharged` 该不该手动设回 `false`（Karst 文档的写法 vs 引擎语义）。
4. `OnFire` 里调 `Fire` 的递归问题（按 Hook 位置**推理**必然递归，但**没有实测**）。
5. `RadioClass` 的 4 个方法**已在 §2.6 列出签名**，但返回值语义与调用时机未验证。
6. `Drawing.cs` / `Surface.cs` / `Blitters.cs` / `TacticalClass.cs` / `Drawing` 相关的绘制 API —— 未展开。
7. `Components\ComponentManager.cs`（13 行）、`Components\Transform.cs`（59 行）、
   `Components\INIComponent.cs`（177 行）、`Utilities\PartialHelper.cs`（70 行）、
   `Utilities\Serialization.cs`（35 行）、`Utilities\SwizzleablePointer.cs`（80 行）—— 未细看。
8. `CCFileClass`（51 行）能否让脚本读取自定义 INI 文件 —— 未看。

---

## 11. 给积木库的"最小可用积木"建议（按风险从低到高）

| 风险 | 积木 | 生成的核心代码 |
| --- | --- | --- |
| 🟢 极低 | 每帧回调 | `public override void OnUpdate() { ... }` |
| 🟢 极低 | 打日志 | `Logger.Log("...", a, b);` |
| 🟢 极低 | 读自身坐标 | `Owner.OwnerObject.Ref.Base.Base.GetCoords()` |
| 🟢 极低 | 读自身血量 / 最大血量 | `Owner.OwnerObject.Ref.Base.Health` / `Owner.OwnerObject.Ref.Type.Ref.Base.Strength` |
| 🟢 极低 | 读自身类型 ID | `Owner.OwnerObject.Ref.Type.Ref.Base.Base.ID` |
| 🟢 极低 | 读自身 House | `Owner.OwnerObject.Ref.Owner` |
| 🟢 极低 | 判存活 / InLimbo | `Owner.OwnerObject.Ref.Base.Base.IsDead()` / `Ref.Base.InLimbo` |
| 🟢 极低 | 读朝向（弧度） | `Owner.OwnerObject.Ref.Facing.current().radians()` |
| 🟢 极低 | 随机数 | `MathEx.Random.Next(min, max)`（`using PatcherYRpp.Utilities;`） |
| 🟢 极低 | 距离 | `a.DistanceFrom(b)` |
| 🟢 极低 | 帧号 / 间隔 | `Game.CurrentFrame` |
| 🟢 极低 | 数学 | `MathEx.Clamp(v, lo, hi)`、`System.Math.*` |
| 🟢 极低 | 遍历全部单位 | `ref var arr = ref TechnoClass.Array;` + `for` |
| 🟡 低 | 受伤回调里改伤害 | `pDamage.Ref = pDamage.Ref / 2;`（⚠️ 先判 `pDamage.IsNotNull`） |
| 🟡 低 | 邻格 / 格子中心 | `CellClass.Coord2Cell(pos)` → `MapClass.Instance.TryGetCellAt` → `cell.Ref.GetCenterCoords()` |
| 🟡 低 | 移动 | `CastToFoot` → `pFoot.Ref.MoveTo(coords)` |
| 🟡 低 | 指令 | `Ref.BaseMission.QueueMission(Mission.Guard, true)` |
| 🟡 中 | 范围伤害 | `MapClass.DamageArea(coords, dmg, pSrc, pWH, false, pHouse)`（`static`） |
| 🟡 中 | 单点伤害 | `pObj.Ref.TakeDamage(dmg, pWH, false)`（⚠️ §2.2 悬垂指针） |
| 🟡 中 | 按名找武器/弹头 | `WeaponTypeClass.ABSTRACTTYPE_ARRAY.Find("X")`（**必须判空**） |
| 🟡 中 | 激光 | `YRMemory.Create<LaserDrawClass>(7 参)`（⚠️ 类型要精确、会抛） |
| 🟡 中 | 超武 | `house.FindSuperWeapon(swType)` + `IsCharged` + `Launch(CellClass.Coord2Cell(pos), true)` |
| 🟡 中 | 挂带逻辑的状态 | `Owner.DecoratorComponent.CreateDecorator<MyDecorator>(id, "desc")`（⚠️ 无参构造） |
| 🟠 较高 | 造子弹 | `pWeapon.Ref.Projectile.Ref.CreateBullet(...)`（⚠️ 双重空指针风险） |
| 🟠 较高 | 动画 | `YRMemory.Create<AnimClass>(pAnimType, coords)`（❓ 待实测） |
| 🔴 高 | 保存别的对象引用 | 用 `ExtensionReference<TechnoExt>`，**绝不要**存裸 `Pointer<T>` |
| ⛔ 不可用 | 音效 / 语音 | 无 API |
| ⛔ 不可用 | 读自定义 INI | 无脚本侧入口 |
| ⛔ 不可用 | `OnRender` / `OnDestroy` | 无驱动 |

---

## 12. 一页速查（照抄区）

```csharp
// ===== 骨架 =====
public class X : TechnoScriptable
{
    public X(TechnoExt owner) : base(owner) { }

    public override void OnUpdate() { }
    public override void OnPut(CoordStruct coord, Direction faceDir) { }        // faceDir 恒为 default
    public override void OnRemove() { }                                        // 别读引擎字段
    public override void OnReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter,
        Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker,
        bool IgnoreDefenses, bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse) { }
    public override void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex) { }
}

// ===== 自身 =====
Pointer<TechnoClass> self   = Owner.OwnerObject;
CoordStruct          pos    = self.Ref.Base.Base.GetCoords();
int                  hp     = self.Ref.Base.Health;
int                  maxHp  = self.Ref.Type.Ref.Base.Strength;
string               typeId = self.Ref.Type.Ref.Base.Base.ID;
Pointer<HouseClass>  house  = self.Ref.Owner;
bool                 dead   = self.Ref.Base.Base.IsDead();
double               facing = self.Ref.Facing.current().radians();
CellStruct           cell   = CellClass.Coord2Cell(pos);

// ===== 别的对象 =====
if (pTarget.CastToTechno(out Pointer<TechnoClass> pT) && !pT.IsNull) { }
if (pTarget.CastToFoot(out Pointer<FootClass> pF) && !pF.IsNull) { }
TechnoExt ext = TechnoExt.ExtMap.Find(pT);            // null 可能
Pointer<TechnoClass> p2 = pT.Convert<TechnoClass>();
bool eq = (pT == p2);

// ===== 查找类型 =====
Pointer<WeaponTypeClass>    pw  = WeaponTypeClass.ABSTRACTTYPE_ARRAY.Find("RedEye2");
Pointer<WarheadTypeClass>   pwh = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find("BlimpHE");
Pointer<BulletTypeClass>    pbt = BulletTypeClass.ABSTRACTTYPE_ARRAY.Find("InvisibleLow");
Pointer<SuperWeaponTypeClass> psw = SuperWeaponTypeClass.ABSTRACTTYPE_ARRAY.Find("IronCurtain");
Pointer<AnimTypeClass>      pan = AnimTypeClass.ABSTRACTTYPE_ARRAY.Find("EXPLOLRG");

// ===== 伤害 =====
MapClass.DamageArea(pos, 100, self, pwh, false, house);              // static，范围
pwh.Ref.CellSpread;                                                   // float

// ===== 子弹 =====
Pointer<BulletClass> pb = pw.Ref.Projectile.Ref.CreateBullet(
    self.Convert<AbstractClass>(), self, 100, pwh, 100, true);        // ⚠️ 会崩，别乱调
if (pb.IsNotNull) { pb.Ref.MoveTo(pos + new CoordStruct(0, 0, 2000), new BulletVelocity(0, 0, 0));
                    pb.Ref.Detonate(pos); }

// ===== 特效 =====
Pointer<LaserDrawClass> laser = YRMemory.Create<LaserDrawClass>(
    fromPos, toPos, new ColorStruct(208, 10, 20), new ColorStruct(88, 0, 20),
    new ColorStruct(10, 10, 10), 50);
if (laser.IsNotNull) { laser.Ref.Thickness = 6; laser.Ref.IsHouseColor = true; }

// ===== 超武 =====
Pointer<SuperClass> pSup = house.Ref.FindSuperWeapon(psw);
if (pSup.IsNotNull) { pSup.Ref.IsCharged = true; pSup.Ref.Launch(cell, true); }

// ===== 随机 / 数学 =====
int r = MathEx.Random.Next(-600, 600);
int c = MathEx.Clamp(r, -100, 100);
double d = pos.DistanceFrom(otherPos);

// ===== 装饰器 =====
public class MyDec : EventDecorator { public override void OnUpdate() { } }   // 无参构造
MyDec dec = Owner.DecoratorComponent.CreateDecorator<MyDec>(new DecoratorId(1), "d");
MyDec got = Owner.DecoratorComponent.Get<MyDec>(new DecoratorId(1));
Owner.DecoratorComponent.Remove(new DecoratorId(1));

// ===== 日志 =====
Logger.Log("hp={0} pos={1}", hp, pos);      // pos 走 CoordStruct.ToString() → "(x, y, z)"
Logger.LogWarning("...");  Logger.LogError("...");  Logger.PrintException(e);
```

---

*清单完。凡是标 ❓ 的部分，请以"进游戏实测"为准再决定是否做成积木。*
