// 由 Karst 脚本工作室生成 —— 用积木搭出来的脚本。
// 放到 <游戏目录>\KarstPatcher\Scripts\ 下即可，Karst 会自动编译。
//
// 下面这行是积木数据，用来"再次编辑"时还原积木视图。
// 删掉它脚本照样能编译，但页面就只能当普通代码编辑器用了：
// @karst-studio {"v":1,"cls":"StudioDemoMissileRain","base":"techno","binding":"MTNK","events":[{"id":"OnFire","conds":[],"acts":[{"t":"rememberTarget","a":{},"i":"b1"},{"t":"log","a":{"level":"info","msg":"锁定目标"},"i":"b2"}]},{"id":"OnUpdate","conds":[{"t":"hasRemembered","a":{},"i":"b3"},{"t":"everyNFrames","a":{"n":15},"i":"b4"}],"acts":[{"t":"damageRemembered","a":{"wh":"BlimpHE","dmg":20},"i":"b5"},{"t":"logRememberedPos","a":{},"i":"b6"}]}]}

using System;
using DynamicPatcher;            // Logger
using Extension.Ext;             // TechnoExt
using Extension.Script;          // TechnoScriptable
using Extension.Utilities;       // ExtensionReference（记住别的对象时要用）
using PatcherYRpp;               // Pointer<T> / CoordStruct 等
using PatcherYRpp.Utilities;     // MathEx（随机数等）

namespace Scripts
{
    public class StudioDemoMissileRain : TechnoScriptable
    {
        public StudioDemoMissileRain(TechnoExt owner) : base(owner) { }

        // 记住的目标（"记住目标"积木写它，"打击记住的目标"读它）
        private ExtensionReference<TechnoExt> _rememberedTarget = new ExtensionReference<TechnoExt>();

        public override void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex)
        {
            // 存下目标。ExtensionReference 内部是弱引用，目标死了 Get() 会返回 null
            if (pTarget.CastToTechno(out Pointer<TechnoClass> tgt_b1))
            {
                TechnoExt ext_b1 = TechnoExt.ExtMap.Find(tgt_b1);
                if (ext_b1 != null)
                {
                    _rememberedTarget.Set(ext_b1);
                }
            }
            Logger.Log("锁定目标");
        }

        public override void OnUpdate()
        {
            Pointer<TechnoClass> self = Owner.OwnerObject;

            if (_rememberedTarget.Get() != null)
            {
                if ((Game.CurrentFrame % 15) == 0)
                {

                    // 打击记住的目标
                    TechnoExt tgt_b5 = _rememberedTarget.Get();
                    if (tgt_b5 != null)
                    {
                        Pointer<TechnoClass> ptgt_b5 = tgt_b5.OwnerObject;
                        if (!ptgt_b5.IsNull)
                        {
                            Pointer<WarheadTypeClass> wh_b5 = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find("BlimpHE");
                            if (!wh_b5.IsNull)
                            {
                                MapClass.DamageArea(ptgt_b5.Ref.Base.Base.GetCoords(), 20, self, wh_b5, false, self.Ref.Owner);
                            }
                        }
                    }

                    TechnoExt tgt_b6 = _rememberedTarget.Get();
                    if (tgt_b6 != null && !tgt_b6.OwnerObject.IsNull)
                    {
                        CoordStruct tpos_b6 = tgt_b6.OwnerObject.Ref.Base.Base.GetCoords();
                        Logger.Log("目标坐标 {0},{1},{2}", tpos_b6.X, tpos_b6.Y, tpos_b6.Z);
                    }
                }
            }
        }
    }
}
