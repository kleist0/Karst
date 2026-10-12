// 由 Karst 脚本工作室生成 —— 用积木搭出来的脚本。
// 放到 <游戏目录>\KarstPatcher\Scripts\ 下即可，Karst 会自动编译。
//
// 下面这行是积木数据，用来"再次编辑"时还原积木视图。
// 删掉它脚本照样能编译，但页面就只能当普通代码编辑器用了：
// @karst-studio {"v":1,"cls":"StudioDemoArmor","base":"techno","binding":"MTNK","events":[{"id":"OnReceiveDamage","conds":[{"t":"damageOver","a":{"n":20},"i":"b1"}],"acts":[{"t":"modifyDamage","a":{"mode":"mul","v":50},"i":"b2"},{"t":"log","a":{"level":"warn","msg":"伤害减半"},"i":"b3"}]},{"id":"OnUpdate","conds":[{"t":"hpBelow","a":{"n":30},"i":"b4"},{"t":"everyNFrames","a":{"n":30},"i":"b5"},{"t":"chance","a":{"n":25},"i":"b6"}],"acts":[{"t":"damageAreaAtSelf","a":{"wh":"BlimpHE","dmg":150},"i":"b7"},{"t":"log","a":{"level":"error","msg":"重伤自爆！"},"i":"b8"}]}]}

using System;
using DynamicPatcher;            // Logger
using Extension.Ext;             // TechnoExt
using Extension.Script;          // TechnoScriptable
using Extension.Utilities;       // ExtensionReference（记住别的对象时要用）
using PatcherYRpp;               // Pointer<T> / CoordStruct 等
using PatcherYRpp.Utilities;     // MathEx（随机数等）

namespace Scripts
{
    public class StudioDemoArmor : TechnoScriptable
    {
        public StudioDemoArmor(TechnoExt owner) : base(owner) { }

        public override void OnReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter,
                    Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker,
                    bool IgnoreDefenses, bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse)
        {
            if (pDamage.Ref > 20)
            {
                pDamage.Ref = (pDamage.Ref * 50) / 100;
                if (pDamage.Ref < 0) { pDamage.Ref = 0; }
                Logger.LogWarning("伤害减半");
            }
        }

        public override void OnUpdate()
        {
            Pointer<TechnoClass> self = Owner.OwnerObject;

            if ((self.Ref.Base.Health * 100) < (30 * self.Ref.Type.Ref.Base.Strength))
            {
                if ((Game.CurrentFrame % 30) == 0)
                {
                    if (MathEx.Random.Next(0, 100) < 25)
                    {

                        // 范围伤害：弹头找不到就什么都不做（Find 找不到会返回空指针）
                        Pointer<WarheadTypeClass> wh_b7 = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find("BlimpHE");
                        if (!wh_b7.IsNull)
                        {
                            MapClass.DamageArea(self.Ref.Base.Base.GetCoords(), 150, self, wh_b7, false, self.Ref.Owner);
                        }
                        Logger.LogError("重伤自爆！");
                    }
                }
            }
        }
    }
}
