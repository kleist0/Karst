using KarstPatcher;
using Extension.Components;
using Extension.Decorators;
using Extension.Script;
using Extension.Utilities;
using PatcherYRpp;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace Extension.Ext
{
    [Serializable]
    public partial class TechnoExt : Extension<TechnoClass>, IHaveComponent
    {
        public static Container<TechnoExt, TechnoClass> ExtMap = new Container<TechnoExt, TechnoClass>("TechnoClass");

        ExtensionReference<TechnoTypeExt> type;
        public TechnoTypeExt Type
        {
            get
            {
                if (type.TryGet(out TechnoTypeExt ext) == false)
                {
                    type.Set(OwnerObject.Ref.Type);
                    ext = type.Get();
                }
                return ext;
            }
        }

        private ExtComponent<TechnoExt> _extComponent;
        private DecoratorComponent _decoratorComponent;
        public ExtComponent<TechnoExt> ExtComponent => _extComponent.GetAwaked();
        public DecoratorComponent DecoratorComponent => _decoratorComponent;
        public Component AttachedComponent => ExtComponent;

        /// <summary>KARST ADDITION: never wakes the component (see the interface).</summary>
        public Component AttachedComponentIfAwaked =>
            _extComponent.IsAwaked ? _extComponent : null;

        /// <summary>
        /// KARST ADDITION: OnPut has no engine hook of its own here
        /// (ObjectClass::Put is virtual, so there is no fixed address), and
        /// adding another hook would widen the conflict surface. Instead the
        /// driver fires it once, on the first frame after creation - by then
        /// the object is in the world and its coordinates are valid.
        /// </summary>
        public bool OnPutFired;

        public TechnoExt(Pointer<TechnoClass> OwnerObject) : base(OwnerObject)
        {

            _extComponent = new ExtComponent<TechnoExt>(this, 0, "TechnoExt root component");
            _decoratorComponent = new DecoratorComponent();
            _extComponent.OnAwake += () => ScriptManager.CreateScriptableTo(_extComponent, Type.Scripts, this);
            _extComponent.OnAwake += () => _decoratorComponent.AttachToComponent(_extComponent);
        }

        public override void OnDeserialization(object sender)
        {
            base.OnDeserialization(sender);
        }

        [OnSerializing]
        protected void OnSerializing(StreamingContext context) { }

        [OnSerialized]
        protected void OnSerialized(StreamingContext context) { }

        [OnDeserializing]
        protected void OnDeserializing(StreamingContext context) { }

        [OnDeserialized]
        protected void OnDeserialized(StreamingContext context) { }

        public override void SaveToStream(IStream stream)
        {
            base.SaveToStream(stream);
            _extComponent.Foreach(c => c.SaveToStream(stream));
        }

        public override void LoadFromStream(IStream stream)
        {
            base.LoadFromStream(stream);
            _extComponent.Foreach(c => c.LoadFromStream(stream));
        }

        [Hook(HookType.AresHook, Address = 0x6F3260, Size = 5)]
        static public unsafe UInt32 TechnoClass_CTOR(REGISTERS* R)
        {
            var pItem = (Pointer<TechnoClass>)R->ESI;

            TechnoExt.ExtMap.FindOrAllocate(pItem);
            return 0;
        }

        // KARST ADDITION: drives the scripts' OnFire.
        //
        // The reference sources never call OnFire (the same gap as OnUpdate),
        // so scripts that react to firing - custom projectiles, lasers, super
        // weapons - could never run. TechnoClass::Fire_NotVirtual is where every
        // shot passes, including the virtual Fire().
        //
        // Size 6 matches Kratos' hook on the same address (Ares and Phobos do not
        // hook it). The chain station saves the existing jmp and runs it after our
        // handler returns 0, so Kratos still sees the shot.
        // KARST ADDITION: drives the scripts' OnReceiveDamage.
        //
        // 0x701900 is the function entry. Phobos hooks the same address
        // (TechnoClass_ReceiveDamage_Shield, size 6), so size 6 is used here
        // as well and the chain station will run Phobos' handler after ours.
        //
        // Argument offsets verified against the disassembly: after
        // `sub esp,0xb4 / push ebx`, the first read is [esp+0xbc], which is
        // the original [esp+4] - i.e. pDamage, matching the signature.
        [Hook(HookType.AresHook, Address = 0x701900, Size = 6)]
        static public unsafe UInt32 TechnoClass_ReceiveDamage(REGISTERS* R)
        {
            var pThis = (Pointer<TechnoClass>)R->ECX;
            var pDamage = R->Stack<Pointer<int>>(0x4);
            int distanceFromEpicenter = R->Stack<int>(0x8);
            var pWH = R->Stack<Pointer<WarheadTypeClass>>(0xC);
            var pAttacker = R->Stack<Pointer<ObjectClass>>(0x10);
            bool ignoreDefenses = R->Stack<int>(0x14) != 0;
            bool preventPassengerEscape = R->Stack<int>(0x18) != 0;
            var pAttackingHouse = R->Stack<Pointer<HouseClass>>(0x1C);

            TechnoExt ext = TechnoExt.ExtMap.Find(pThis);

            if (ext != null)
                ScriptManager.OnReceiveDamageScripts(ext, pDamage,
                    distanceFromEpicenter, pWH, pAttacker, ignoreDefenses,
                    preventPassengerEscape, pAttackingHouse);

            return 0;
        }

        [Hook(HookType.AresHook, Address = 0x6FDD50, Size = 6)]
        static public unsafe UInt32 TechnoClass_Fire(REGISTERS* R)
        {
            var pThis = (Pointer<TechnoClass>)R->ECX;
            var pTarget = R->Stack<Pointer<AbstractClass>>(0x4);
            int weaponIndex = R->Stack<int>(0x8);

            TechnoExt ext = TechnoExt.ExtMap.Find(pThis);

            if (ext != null)
                ScriptManager.OnFireScripts(ext, pTarget, weaponIndex);

            return 0;
        }

        [Hook(HookType.AresHook, Address = 0x6F4500, Size = 5)]
        static public unsafe UInt32 TechnoClass_DTOR(REGISTERS* R)
        {
            var pItem = (Pointer<TechnoClass>)R->ECX;

            // KARST ADDITION: notify the scripts before the extension is torn
            // down. Reuses the DTOR hook that was already in place, so no extra
            // address is taken.
            //
            // This whole body is guarded, and that is not defensive padding: the
            // same hook also removes the entry from ExtMap, so an exception that
            // escapes does not merely lose one notification - the hook UNHOOKS
            // ITSELF and the map leaks for the rest of the session.
            //
            // Measured failure this prevents: dispatching here went through
            // AttachedComponent, which WAKES the component, so OnAwake ran against
            // an object that was already being destroyed, read a dead Type, threw,
            // and cost us the DTOR hook (followed by C0000005 and a game exit).
            try
            {
                TechnoExt remExt = TechnoExt.ExtMap.Find(pItem);

                if (remExt != null)
                    ScriptManager.OnRemoveScripts(remExt);
            }
            catch (Exception e)
            {
                Logger.PrintException(e);
            }

            try
            {
                TechnoExt.ExtMap.Remove(pItem);
            }
            catch (Exception e)
            {
                Logger.PrintException(e);
            }

            return 0;
        }

        //[Hook(HookType.AresHook, Address = 0x70C250, Size = 8)]
        //[Hook(HookType.AresHook, Address = 0x70BF50, Size = 5)]
        static public unsafe UInt32 TechnoClass_SaveLoad_Prefix(REGISTERS* R)
        {
            var pItem = R->Stack<Pointer<TechnoClass>>(0x4);
            var pStm = R->Stack<Pointer<IStream>>(0x8);
            IStream stream = Marshal.GetObjectForIUnknown(pStm) as IStream;

            TechnoExt.ExtMap.PrepareStream(pItem, stream);
            return 0;
        }

        //[Hook(HookType.AresHook, Address = 0x70C249, Size = 5)]
        static public unsafe UInt32 TechnoClass_Load_Suffix(REGISTERS* R)
        {
            TechnoExt.ExtMap.LoadStatic();
            return 0;
        }

        //[Hook(HookType.AresHook, Address = 0x70C264, Size = 5)]
        static public unsafe UInt32 TechnoClass_Save_Suffix(REGISTERS* R)
        {
            TechnoExt.ExtMap.SaveStatic();
            return 0;
        }
    }

}
