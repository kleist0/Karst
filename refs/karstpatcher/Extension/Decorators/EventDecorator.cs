using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Extension.Decorators
{
    /// <summary>
    /// A decorator that reacts to events.
    ///
    /// WHY THIS EXISTS
    ///
    /// The reference project's samples use `EventDecorator` (for example
    /// GGIScript's MissileFall), but the class is NOT in the PatcherExtension
    /// repository - so those samples cannot compile ("CS0246: EventDecorator
    /// could not be found"). The plain `Decorator` here carries only data
    /// (Description / ID / Decorative) and has no behaviour at all.
    ///
    /// This supplies the missing base: a decorator that can be driven per frame,
    /// the same way scripts are. Karst's driver walks each extension's decorators
    /// and calls OnUpdate, so a decorator is a way to attach per-object state with
    /// its own update logic - without adding another Component to the tree.
    ///
    /// The set of events mirrored here is deliberately the same as the script
    /// events that are actually driven (see ScriptManager), so there is no
    /// method that would silently never be called.
    /// </summary>
    [Serializable]
    public abstract class EventDecorator : Decorator
    {
        /// <summary>Called once per frame for as long as the decorator lives.</summary>
        public virtual void OnUpdate() { }

        /// <summary>
        /// Called when the decorator is removed. Override to release anything that
        /// does not belong to the managed heap.
        /// </summary>
        public virtual void OnRemove() { }
    }
}
