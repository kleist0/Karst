// ===========================================================================
//  Karst 脚本示例 1：最小可运行脚本
// ===========================================================================
//  这个文件验证过可以编译、可以运行（见 docs\脚本编写.md）。
//
//  用法：
//    1. 把本文件放到 <游戏目录>\KarstPatcher\Scripts\
//    2. 在 rulesmd.ini 给目标单位加一行：  [MTNK] Scripts=MyFirst
//    3. 进游戏造一辆灰熊坦克
//    4. 观察 <游戏目录>\KarstPatcher\patcher.log 会不断出现 "MyFirst tick N"
//
//  注意：`Scripts=` 的值是**类名**（MyFirst），不是文件名。
// ===========================================================================

using System;
using DynamicPatcher;            // Logger
using Extension.Ext;             // TechnoExt
using Extension.Script;          // TechnoScriptable
using PatcherYRpp;               // Pointer、CoordStruct 等

namespace Scripts
{
    public class MyFirst : TechnoScriptable
    {
        public MyFirst(TechnoExt owner) : base(owner) { }

        private int _tick = 0;

        public override void OnUpdate()
        {
            // 每帧调用。这里只打印前几次，避免刷屏。
            if (_tick++ < 5)
                Logger.Log("MyFirst tick {0}", _tick);
        }

        public override void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex)
        {
            Logger.Log("MyFirst fired with weapon index {0}", weaponIndex);
        }

        public override void OnRemove()
        {
            Logger.Log("MyFirst removed");
        }
    }
}
