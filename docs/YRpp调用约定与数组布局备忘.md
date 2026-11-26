# YRpp 调用约定与数组布局备忘

> 2026-11-26，在给 Karst 写 `EmptyReloadSound` 时连续踩到三类问题，全部实测。
> 这类问题**不会编译报错**，只表现为"静默无效"或"返回垃圾值"，极难排查。
> 写完任何"调用引擎函数/读引擎数组"的功能后，建议按本文 §4 的清单过一遍。

---

## 1. `__fastcall` 声明 + `JMP_STD` 转发

YRpp 里有一批静态函数声明为 `__fastcall`，函数体却用 `JMP_STD` 转发（stdcall：参数压栈）。
x86 下 fastcall 的第一个参数在 `ECX`、第二个在 `EDX`；stdcall 全在栈上。于是：

- 被调函数从 `ECX`/`EDX` 取到的是垃圾；
- 返回值也是垃圾。

### 案例 A：`VocClass::FindIndex`

```cpp
static int __fastcall FindIndex(const char *pName)
    { JMP_STD(0x7514D0); }   // 参数压栈，但函数读 ECX
```

实测：`FindIndex("Explosion10")` 返回 **255**（应为 -1 或真实索引）。

反汇编 `0x7514D0`：

```
0x7514D0  push esi
0x7514D1  push edi
0x7514D2  mov  edi, ecx          ; ← 名字在 ECX（fastcall）
0x7514FF  call 0x7C8D20          ; _strcmpi
0x751516  or   eax, 0xffffffff   ; 未找到返回 -1
0x75151A  ret                    ; 无栈清理
```

正确调用：

```cpp
using FindIndexFn = int(__fastcall*)(const char*);
static FindIndexFn const fn = reinterpret_cast<FindIndexFn>(0x7514D0);
int index = fn("NukeExplosion");   // 实测 223
```

### 案例 B：`VocClass::PlayAt`

```cpp
static void __fastcall PlayAt(int n, const CoordStruct& coords, AudioController* pCtrl = nullptr)
    { JMP_STD(0x7509E0); }
```

反汇编 `0x7509E0`：

```
0x7509E0  mov  al, [0x8464AC]    ; 音效总开关
0x7509E5  push ebx
0x7509E8  mov  ebx, edx          ; ← coords 指针在 EDX
0x7509F9  test ecx, ecx          ; ← 音效索引在 ECX
0x750A0F  mov  ebp, [esp+0x14]   ; ← AudioController* 在栈上
0x7509EF  ret  4                 ; 清理 1 个栈参数
```

所以真实约定是 `__fastcall(int n, const CoordStruct* coords, AudioController*)`，
**第三个参数在栈上**、函数自己 `ret 4`。

正确调用：

```cpp
using PlayAtFn = void(__fastcall*)(int, const CoordStruct*, void*);
static PlayAtFn const fn = reinterpret_cast<PlayAtFn>(0x7509E0);
fn(index, &coords, nullptr);
```

---

## 2. 数组布局不一致

`VocClass::Array` 在 `StaticInits.cpp` 里绑定到 `0xB1D378`：

```cpp
DECL(VocClass, 0xB1D378);   // 被当成 DynamicVectorClass<VocClass*>*
```

但引擎自己的 `FindIndex` 用的是：

```
0x7514D8  mov eax, [0xB1D388]   ; count
0x7514E3  mov eax, [0xB1D37C]   ; items
```

即 items 在 `0xB1D37C`、count 在 `0xB1D388`——与 `0xB1D378` 的 `DynamicVectorClass` 布局对不上。

后果：通过 `VocClass::Array->Count` 读到 **0**，于是任何名字都"找不到"（`index = -1`）。

### 修复优先级

1. **首选**：调用引擎自己的函数（按真实约定），不要自己读数组；
2. 其次：按引擎反汇编里的真实地址读（`0xB1D37C` / `0xB1D388`），并核对字段偏移；
3. 不要相信 YRpp 的 `Array->Count`。

---

## 3. 判断一个函数真实约定的方法

1. 反汇编入口，看参数从哪来：
   - `mov reg, ecx` / 直接用 `ecx` → fastcall / thiscall；
   - 只在 `[esp+4]`、`[esp+8]` 读 → stdcall；
2. 看 `ret N`：`ret 0` = 无栈参数；`ret 4` = 一个 4 字节栈参数；以此类推；
3. 对照 YRpp 声明：声明 `__fastcall` 而反汇编读栈，或用了 `JMP_STD` 而反汇编读 `ECX`，就是坑。

---

## 4. 检查清单（写完一个"调用引擎函数"的功能后）

- [ ] 用一个**已知存在**的名字/对象测试，确认返回真实索引/指针（不是 255 / 0 / -1）；
- [ ] 如果用了 YRpp 的 `Array`，先打印 `Array->Count`，确认不是 0；
- [ ] 用一次"名字不存在"的调用，确认返回值是 -1（而不是垃圾值）；
- [ ] 播放/伤害类调用先用最小参数试一次，确认没有静默失败。

---

## 5. 其它已知的同类坑

| 坑 | 表现 | 规避 |
| --- | --- | --- |
| `VocClass::FindIndex` / `PlayAt`（本文） | 返回 255 / 静默不播 | 按 fastcall 直接调 0x7514D0 / 0x7509E0 |
| `TechnoClass::Veterancy` 偏移与 YRpp 不符 | `IsElite()` 读到野值，后续崩溃 | 不读 Veterancy；改用普通武器槽 |
| `TechnoTypeClass::Weapon` 数组偏移与 YRpp 不符 | `GetWeapon(0/1, false).WeaponType` 返回 null | 不按武器取配置，改按单位类型 |
| 函数中途改了 ESI | `0x6FB160` 处 ESI 是 `this + 0x1FC`（装填计时器），不是 `TechnoClass*` | hook 里先 `- 0x1FC` 还原对象指针 |

---

## 6. 本次最终实现（供参考）

`EmptyReloadSound`（`src/Ext/EmptyReloadSound/`）最终：

- 钩子：`0x6FB0C9`（空弹装填分支）；
- 查音效：`0x7514D0`（fastcall）；
- 播声音：`0x7509E0`（fastcall）；
- 播动画：`AnimTypeClass::Find`（该绑定实测可用）+ `GameCreate<AnimClass>`；
- 配置：单位段 `EmptyReloadSound=`（soundmd 条目名）、`EmptyReloadAnim=`（artmd 段名）。
