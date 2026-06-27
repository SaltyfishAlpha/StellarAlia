namespace StellarAlia;

/// <summary>
/// Runtime control of the InputSystem map stack (Issue #71 Phase 3a). Pair with
/// <see cref="InputAction"/> to switch the named-action set seen by the script
/// (pause menus, vehicle/on-foot mode swaps, level-specific control schemes).
///
/// All names refer to <c>ActionMapDef.name</c> values from project
/// <c>.sainputmap</c> assets (NOT file names). Maps unknown to the InputSystem
/// log a warning on the engine side; the script methods return false / empty.
/// </summary>
public static class InputMap
{
    /// <summary>Push the named map on top of the stack. Returns false when no
    /// map with this name is registered.</summary>
    public static bool Push(string name)    => NativeApi.SA_InputMap_Push(name) != 0;

    /// <summary>Pop the top map. No-op when the stack is already empty.</summary>
    public static void Pop()                 => NativeApi.SA_InputMap_Pop();

    /// <summary>Clear the stack and push the named map. Returns false on unknown name.</summary>
    public static bool Replace(string name)  => NativeApi.SA_InputMap_Replace(name) != 0;

    /// <summary>True when any layer of the stack references this map name.
    /// For "is this specifically the top map?" use <see cref="GetActive"/>.</summary>
    public static bool IsActive(string name) => NativeApi.SA_InputMap_IsActive(name) != 0;

    /// <summary>Name of the current top-of-stack map, or empty string when the
    /// stack is empty (game scripts not yet reached PIE entry).</summary>
    public static string GetActive()         => NativeApi.SA_InputMap_GetActive();
}
