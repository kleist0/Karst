// ===========================================================================
//  Karst 脚本示例 2：开火后给目标下"追踪弹雨"
// ===========================================================================
//  这个文件验证过可以编译（compile file succeed! / appling: MissileRain）。
//
//  效果：单位开火时会记住目标，之后每帧在目标头顶附近生成一发小威力的
//        空中子弹并引爆，形成持续的火力压制。
//
//  用法：
//    [MTNK] Scripts=MissileRain
//
//  它演示了三件常用的事：
//    * 用 ExtensionReference 保存"另一个对象"的引用
//    * 在 OnFire 里拿到开火目标（CastToTechno）
//    * 创建子弹并让它去某个坐标（CreateBullet + MoveTo）
// ===========================================================================

using System;
using KarstPatcher;            // Logger
using Extension.Ext;             // TechnoExt
using Extension.Script;          // TechnoScriptable
using Extension.Utilities;       // ExtensionReference
using PatcherYRpp;               // Pointer、CoordStruct、WeaponTypeClass...
using PatcherYRpp.Utilities;     // MathEx

namespace Scripts
{
    public class MissileRain : TechnoScriptable
    {
        public MissileRain(TechnoExt owner) : base(owner) { }

        // 保存目标的引用。ExtensionReference 会跟踪目标是否还有效，
        // 不要直接存 Pointer<T>——对象销毁后指针就失效了。
        private ExtensionReference<TechnoExt> _target = new ExtensionReference<TechnoExt>();

        private const string WeaponId = "RedEye2";
        private const string WarheadId = "BlimpHE";

        public override void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex)
        {
            if (_target.Get() != null)
                return;   // 已经有目标了，不重复记录

            if (pTarget.CastToTechno(out Pointer<TechnoClass> pTechno))
                _target.Set(TechnoExt.ExtMap.Find(pTechno));
        }

        public override void OnUpdate()
        {
            TechnoExt target = _target.Get();

            if (target == null)
                return;   // 目标没了（被打掉 / 离开地图）

            CoordStruct pos = target.OwnerObject.Ref.Base.Base.GetCoords();
            int r = MathEx.Random.Next(-600, 600);

            Pointer<WeaponTypeClass> pWeapon = WeaponTypeClass.ABSTRACTTYPE_ARRAY.Find(WeaponId);
            Pointer<WarheadTypeClass> pWarhead = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find(WarheadId);

            // 名字写错时 Find 会返回空指针；不检查就会在下一行崩掉。
            if (pWeapon.IsNull || pWarhead.IsNull)
                return;

            Pointer<BulletClass> pBullet = pWeapon.Ref.Projectile.Ref.CreateBullet(
                Owner.OwnerObject.Convert<AbstractClass>(),   // 谁开的火
                Owner.OwnerObject,                            // 攻击者
                10,                                           // 伤害
                pWarhead,                                     // 弹头
                100,                                          // 速度
                false);                                       // 是否可见

            if (!pBullet.IsNull)
            {
                // 从目标上方落下来（Z + 2000）
                pBullet.Ref.MoveTo(
                    pos + new CoordStruct(r, r, 2000),
                    new BulletVelocity(0, 0, 0));
            }
        }
    }
}
