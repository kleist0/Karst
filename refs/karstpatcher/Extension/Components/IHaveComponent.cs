using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Extension.Components
{
    public interface IHaveComponent
    {
        Component AttachedComponent { get; }

        /// <summary>
        /// KARST ADDITION: the component WITHOUT waking it, or null if it was
        /// never awaked. Use this from anything that runs while an object is
        /// being destroyed: there are no scripts to notify if the component
        /// never woke up, and waking it there reads dead engine fields.
        /// </summary>
        Component AttachedComponentIfAwaked { get; }
    }
}
