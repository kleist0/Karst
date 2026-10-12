using DynamicPatcher;
using Extension.Components;
using Extension.Ext;
using PatcherYRpp;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Extension.Script
{
    public partial class ScriptManager
    {
        // type name -> script
        static Dictionary<string, Script> Scripts = new Dictionary<string, Script>();

        /// <summary>
        /// create script or get a exist script by script name
        /// </summary>
        /// <param name="scriptName"></param>
        /// <returns></returns>
        public static Script GetScript(string scriptName)
        {
            if(scriptName == null)
                return null; 

            if (Scripts.TryGetValue(scriptName, out Script script))
            {
                return script;
            }
            else
            {
                Script newScript = new Script(scriptName);
                try
                {
                    Assembly assembly = FindScriptAssembly(scriptName);

                    RefreshScript(newScript, assembly);

                    Scripts.Add(scriptName, newScript);
                    return newScript;
                }
                catch (Exception e)
                {
                    Logger.LogError("ScriptManager could not find script: {0}", scriptName);
                    Logger.PrintException(e);
                    return null;
                }
            }
        }
        /// <summary>
        /// get all scripts in cs file
        /// </summary>
        /// <param name="fileName"></param>
        /// <returns></returns>
        public static List<Script> GetScripts(string fileName)
        {
            List<Script> scripts = new List<Script>();

            var pair = Program.Patcher.FileAssembly.First((pair) => pair.Key.EndsWith(fileName));
            Assembly assembly = pair.Value;

            Type[] types = FindScriptTypes(assembly);

            foreach (var type in types)
            {
                Script script = GetScript(type.Name);
                scripts.Add(script);
            }

            return scripts;
        }
        /// <summary>
        /// get scripts by script names or cs file names
        /// </summary>
        /// <param name="scriptList"></param>
        /// <returns></returns>
        public static List<Script> GetScripts(List<string> scriptList)
        {
            List<Script> scripts = new List<Script>();

            foreach (string item in scriptList)
            {
                if (item.EndsWith(".cs"))
                {
                    scripts.AddRange(GetScripts(item));
                }
                else
                {
                    scripts.Add(GetScript(item));
                }
            }

            return scripts;
        }

        public static void CreateScriptableTo<T>(Component root, IEnumerable<Script> scripts, T owner)
        {
            if (scripts == null)
                return;

            foreach (var script in scripts)
            {
                CreateScriptableTo(root, script, owner);
            }
        }
        public static Scriptable<T> CreateScriptableTo<T>(Component root, Script script, T owner)
        {
            if (script == null)
                return null;

            var scriptComponent = CreateScriptable(script, owner);
            scriptComponent.AttachToComponent(root);
            return scriptComponent;
        }
        public static TScriptable CreateScriptable<TScriptable>(Script script, params object[] parameters) where TScriptable : ScriptComponent
        {
            if (script == null)
                return null;

            var scriptable = Activator.CreateInstance(script.ScriptableType, parameters) as TScriptable;
            scriptable.Script = script;
            return scriptable;
        }

        public static Scriptable<T> CreateScriptable<T>(Script script, T owner)
        {
            return CreateScriptable<Scriptable<T>>(script, owner);
        }

        private static Type[] FindScriptTypes(Assembly assembly)
        {
            Type[] types = assembly.GetTypes();
            if (types == null || types.Length == 0)
                return new Type[0];

            return types.Where(t => typeof(IScriptable).IsAssignableFrom(t)).ToArray();
        }

        private static Assembly FindScriptAssembly(string scriptName)
        {
            foreach (var pair in Program.Patcher.FileAssembly)
            {
                Assembly assembly = pair.Value;
                Type[] types = FindScriptTypes(assembly);
                foreach (Type type in types)
                {
                    if (type.Name == scriptName)
                    {
                        return assembly;
                    }
                }
            }

            return null;
        }

        private static void RefreshScript(Script script, Assembly assembly)
        {
            Type[] types = FindScriptTypes(assembly);
            foreach (Type type in types)
            {
                if (type.Name == script.Name)
                {
                    script.ScriptableType = type;
                    break;
                }
            }
        }

        private static void RefreshScripts(Assembly assembly)
        {
            Type[] types = FindScriptTypes(assembly);

            foreach (Type type in types)
            {
                string scriptName = type.Name;
                if (Scripts.TryGetValue(scriptName, out Script script))
                {
                    RefreshScript(script, assembly);
                }
                else
                {
                    script = GetScript(scriptName);
                }

                Logger.Log("refresh script: {0}", script.Name);
            }

        }


        private static void Patcher_AssemblyRefresh(object sender, AssemblyRefreshEventArgs args)
        {
            Assembly assembly = args.RefreshedAssembly;
            RefreshScripts(assembly);

            Type[] types = FindScriptTypes(assembly);
            foreach (Type type in types)
            {

                // [warning!] unsafe change to scriptable
                unsafe
                {
                    // refresh modified scripts only
                    void RefreshScriptComponents<TExt>(TExt ext) where TExt : IHaveComponent
                    {
                        Component root = ext.AttachedComponent;
                        ScriptComponent[] components = root.GetComponentsInChildren(c => c.GetType().Name == type.Name).Cast<ScriptComponent>().ToArray();
                        if (components.Length > 0)
                        {
                            foreach (var component in components)
                            {
                                component.DetachFromParent();
                            }
                            var scripts = components.Select(c => c.Script);
                            CreateScriptableTo(root, scripts, ext);
                        }
                    }
                    ref var technoArray = ref TechnoClass.Array;
                    for (int i = 0; i < technoArray.Count; i++)
                    {
                        var pItem = technoArray[i];
                        var ext = TechnoExt.ExtMap.Find(pItem);
                        RefreshScriptComponents(ext);
                    }

                    ref var bulletArray = ref BulletClass.Array;
                    for (int i = 0; i < bulletArray.Count; i++)
                    {
                        var pItem = bulletArray[i];
                        var ext = BulletExt.ExtMap.Find(pItem);
                        RefreshScriptComponents(ext);
                    }

                }
            }
        }

        private static void RefreshScriptable<TExt, TBase>() where TExt : IScriptable
        {

        }

        static ScriptManager()
        {
            Program.Patcher.AssemblyRefresh += Patcher_AssemblyRefresh;

            // KARST ADDITION: drive the scripts every frame. Upstream has NO
            // caller for OnUpdate anywhere (verified by search), so scripts compiled,
            // loaded and attached but their per-frame callback never ran.
            Program.Tick += TickExtensions;
        }

        /// <summary>
        /// KARST ADDITION: per-frame driver for every live extension.
        /// </summary>
        /// <summary>
        /// KARST ADDITION: dispatch OnPut using the object's current state.
        /// </summary>
        public static void OnPutScripts(TechnoExt ext)
        {
            if (ext == null)
                return;

            CoordStruct coords = default;
            Direction faceDir = default;

            try
            {
                coords = ext.OwnerObject.Ref.Base.Base.GetCoords();
                // Facing is left at its default: the accessor differs per class
                // in this binding layer, and the coordinate is what OnPut uses.
            }
            catch (Exception)
            {
                // A half-constructed object must not abort the frame.
            }

            DispatchObjectEvent(ext, c =>
            {
                if (c is IObjectScriptable o)
                    o.OnPut(coords, faceDir);
            });
        }

        /// <summary>
        /// KARST ADDITION: dispatch OnReceiveDamage to every script on a unit.
        /// </summary>
        public static void OnReceiveDamageScripts(TechnoExt ext,
            Pointer<int> pDamage, int distanceFromEpicenter, Pointer<WarheadTypeClass> pWH,
            Pointer<ObjectClass> pAttacker, bool ignoreDefenses,
            bool preventPassengerEscape, Pointer<HouseClass> pAttackingHouse)
        {
            DispatchObjectEvent(ext, c =>
            {
                if (c is IObjectScriptable o)
                    o.OnReceiveDamage(pDamage, distanceFromEpicenter, pWH, pAttacker,
                        ignoreDefenses, preventPassengerEscape, pAttackingHouse);
            });
        }

        /// <summary>
        /// KARST ADDITION: dispatch OnRemove before an extension is torn down.
        /// </summary>
        public static void OnRemoveScripts(TechnoExt ext)
        {
            DispatchObjectEvent(ext, c =>
            {
                if (c is IObjectScriptable o)
                    o.OnRemove();
            });
        }

        /// <summary>
        /// KARST ADDITION: dispatch OnDetonate to every script on a bullet.
        /// </summary>
        public static void OnDetonateScripts(BulletExt ext, Pointer<CoordStruct> pCoords)
        {
            DispatchObjectEvent(ext, c =>
            {
                if (c is IBulletScriptable b)
                    b.OnDetonate(pCoords);
            });
        }

        /// <summary>
        /// KARST ADDITION: shared walk over one extension's component tree.
        /// </summary>
        private static void DispatchObjectEvent<TExt>(TExt ext, Action<Component> action)
            where TExt : class, IHaveComponent
        {
            if (ext == null)
                return;

            // KARST FIX: AttachedComponentIfAwaked, not AttachedComponent.
            // This runs from the DTOR as well, and the waking accessor would run
            // OnAwake against a half-destroyed object.
            Component component = ext.AttachedComponentIfAwaked;

            if (component == null)
                return;

            // Per-component isolation: one throwing script must not abort the
            // remaining scripts, and must certainly not reach the hook wrapper
            // (which would unhook the whole hook).
            component.Foreach(c => SafeScriptCall(() => action(c)));
        }

        /// <summary>
        /// KARST ADDITION: runs one script callback, swallowing its exception.
        ///
        /// A script fault used to escape to the hook wrapper, which reacts by
        /// calling UnHook() - so one buggy script silently disabled the entire
        /// hook for the rest of the session. Script errors are the user's to fix;
        /// they must not cost the framework.
        /// </summary>
        private static void SafeScriptCall(Action call)
        {
            try
            {
                call();
            }
            catch (Exception e)
            {
                Logger.PrintException(e);
            }
        }

        /// <summary>
        /// KARST ADDITION: dispatch OnFire to every script attached to a unit.
        /// </summary>
        public static void OnFireScripts(TechnoExt ext, Pointer<AbstractClass> pTarget, int weaponIndex)
        {
            if (ext == null)
                return;

            Component component = ext.AttachedComponent;

            if (component == null)
                return;

            component.Foreach(c =>
            {
                if (c is ITechnoScriptable scriptable)
                    scriptable.OnFire(pTarget, weaponIndex);
            });
        }

        private static void TickExtensions()
        {
            DriveExtensions(TechnoExt.ExtMap.AllExtensions);
            DriveExtensions(BulletExt.ExtMap.AllExtensions);
        }

        private static void DriveExtensions<TExt>(IEnumerable<TExt> extensions)
            where TExt : class, IHaveComponent
        {
            foreach (TExt ext in extensions)
            {
                if (ext == null)
                    continue;

                Component component = ext.AttachedComponent;

                if (component == null)
                    continue;

                // Wake HERE, not in the constructor. Measured: waking during
                // construction made OnAwake read TechnoClass::Type while the
                // object was still being built, so Type was NULL, the
                // NullReferenceException tripped the hook's try/catch, and the
                // hook UNHOOKED ITSELF - only the first unit ever worked.
                component.EnsureAwaked();

                // KARST ADDITION: OnPut has no hook of its own; fire it once,
                // on the first driven frame (the object is in the world by then).
                if (ext is TechnoExt techno && !techno.OnPutFired)
                {
                    techno.OnPutFired = true;
                    OnPutScripts(techno);
                }

                // Component.OnUpdate is an empty virtual, so the tree must be
                // walked explicitly; the script object is a child component.
                component.Foreach(c => SafeScriptCall(() => c.OnUpdate()));

                // KARST ADDITION: event decorators are not Components, so the
                // walk above does not reach them. A decorator is how a script
                // attaches per-object state with its own update logic.
                // Only the techno/bullet extensions carry a DecoratorComponent,
                // hence the type check (the driver itself stays generic).
                SafeScriptCall(() =>
                {
                    if (ext is TechnoExt technoForDecorators)
                        technoForDecorators.DecoratorComponent?.DriveDecorators();
                    else if (ext is BulletExt bulletForDecorators)
                        bulletForDecorators.DecoratorComponent?.DriveDecorators();
                });
            }
        }
    }
}
