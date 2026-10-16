// 由 Karst 脚本工作室生成 —— 用积木搭出来的脚本。
// 放到 <游戏目录>\karstpatcher\Scripts\ 下即可，Karst 会自动编译。
//
// 下面这行是积木数据，用来"再次编辑"时还原积木视图。
// 删掉它脚本照样能编译，但页面就只能当普通代码编辑器用了：
// @karst-studio {"v":1,"cls":"StudioDemoLog","base":"techno","binding":"MTNK","events":[{"id":"OnUpdate","conds":[{"t":"everyNFrames","a":{"n":60},"i":"b1"}],"acts":[{"t":"log","a":{"level":"info","msg":"StudioDemoLog 还活着"},"i":"b2"},{"t":"logSelf","a":{},"i":"b3"}]}]}

using System;
using KarstPatcher;            // Logger
using Extension.Ext;             // TechnoExt
using Extension.Script;          // TechnoScriptable
using Extension.Utilities;       // ExtensionReference（记住别的对象时要用）
using PatcherYRpp;               // Pointer<T> / CoordStruct 等
using PatcherYRpp.Utilities;     // MathEx（随机数等）

namespace Scripts
{
    public class StudioDemoLog : TechnoScriptable
    {
        public StudioDemoLog(TechnoExt owner) : base(owner) { }

        public override void OnUpdate()
        {
            Pointer<TechnoClass> self = Owner.OwnerObject;

            if ((Game.CurrentFrame % 60) == 0)
            {
                Logger.Log("StudioDemoLog 还活着");
                CoordStruct pos_b3 = self.Ref.Base.Base.GetCoords();
                Logger.Log("坐标 {0},{1},{2}  血量 {3}/{4}", pos_b3.X, pos_b3.Y, pos_b3.Z,
                    self.Ref.Base.Health, self.Ref.Type.Ref.Base.Strength);
            }
        }
    }
}
