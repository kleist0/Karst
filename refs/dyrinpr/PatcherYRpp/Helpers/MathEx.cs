using System;

namespace PatcherYRpp.Utilities
{
    /// <summary>
    /// Shared math helpers.
    ///
    /// WHY THIS FILE EXISTS
    ///
    /// Extension's DecoratorComponent calls `MathEx.Random.Next()` to make a
    /// decorator id. MathEx belongs to a separate `PatcherYRpp.Utilities`
    /// project that is NOT part of the PatcherYRpp or PatcherExtension
    /// repositories, so building Extension from those sources alone fails with
    /// "CS0103: MathEx does not exist in the current context".
    ///
    /// Only that one member is used, so this provides exactly it rather than
    /// guessing at the rest of the original class. If the full
    /// PatcherYRpp.Utilities project ever becomes available, replace this file
    /// with it.
    /// </summary>
    public static class MathEx
    {
        /// <summary>
        /// The shared random source.
        ///
        /// Thread-safe construction matters: the original call site is inside
        /// instance code that may run from the game thread, and a `new Random()`
        /// per call would hand out duplicate ids when called in the same tick
        /// (the default seed is time-based).
        /// </summary>
        public static Random Random { get; } = new Random();

        public static int Clamp(int value, int min, int max)
        {
            return value < min ? min : (value > max ? max : value);
        }

        public static double Clamp(double value, double min, double max)
        {
            return value < min ? min : (value > max ? max : value);
        }
    }
}
