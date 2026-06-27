using System.Collections.Generic;
using System.Numerics;

namespace StellarAlia;

/// <summary>
/// Logical key identifiers. Mapped internally to engine device paths
/// (e.g. <see cref="Key.W"/> → "Keyboard/W").
/// </summary>
#pragma warning disable CS1591 // Member names are self-documenting.
public enum Key
{
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    Up, Down, Left, Right,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
}
#pragma warning restore CS1591

/// <summary>Mouse button identifiers.</summary>
public enum MouseButton {
    /// <summary>The primary (left) mouse button.</summary>
    Left,
    /// <summary>The secondary (right) mouse button.</summary>
    Right,
    /// <summary>The middle mouse button / scroll-wheel click.</summary>
    Middle
}

/// <summary>
/// Raw device-path keyboard and mouse polling. For decoupled named actions
/// configured via .sainputmap assets, use <see cref="InputAction"/> instead.
/// </summary>
public static class Input
{
    private static readonly HashSet<Key> _prev = new();
    private static readonly HashSet<Key> _curr = new();

    // Called by ScriptBridgeEntry before OnUpdate each frame to advance frame state.
    internal static void BeginFrame() {
        _prev.Clear();
        foreach (var k in _curr) _prev.Add(k);
        _curr.Clear();
        foreach (Key k in System.Enum.GetValues<Key>()) {
            if (NativeApi.SA_Input_GetKey(KeyPath(k)) > 0.5f) _curr.Add(k);
        }
    }

    private static string KeyPath(Key k) => k switch {
        Key.Space      => "Keyboard/Space",
        Key.Enter      => "Keyboard/Enter",
        Key.Escape     => "Keyboard/Escape",
        Key.Tab        => "Keyboard/Tab",
        Key.Backspace  => "Keyboard/Backspace",
        Key.LeftShift  => "Keyboard/LeftShift",
        Key.RightShift => "Keyboard/RightShift",
        Key.LeftCtrl   => "Keyboard/LeftCtrl",
        Key.RightCtrl  => "Keyboard/RightCtrl",
        Key.LeftAlt    => "Keyboard/LeftAlt",
        Key.RightAlt   => "Keyboard/RightAlt",
        Key.Up         => "Keyboard/Up",
        Key.Down       => "Keyboard/Down",
        Key.Left       => "Keyboard/Left",
        Key.Right      => "Keyboard/Right",
        Key.F1  => "Keyboard/F1",  Key.F2  => "Keyboard/F2",
        Key.F3  => "Keyboard/F3",  Key.F4  => "Keyboard/F4",
        Key.F5  => "Keyboard/F5",  Key.F6  => "Keyboard/F6",
        Key.F7  => "Keyboard/F7",  Key.F8  => "Keyboard/F8",
        Key.F9  => "Keyboard/F9",  Key.F10 => "Keyboard/F10",
        Key.F11 => "Keyboard/F11", Key.F12 => "Keyboard/F12",
        Key.Num0 => "Keyboard/0",  Key.Num1 => "Keyboard/1",
        Key.Num2 => "Keyboard/2",  Key.Num3 => "Keyboard/3",
        Key.Num4 => "Keyboard/4",  Key.Num5 => "Keyboard/5",
        Key.Num6 => "Keyboard/6",  Key.Num7 => "Keyboard/7",
        Key.Num8 => "Keyboard/8",  Key.Num9 => "Keyboard/9",
        _ => $"Keyboard/{k}"
    };

    /// <summary>Returns true while the key is held.</summary>
    public static bool IsKeyDown(Key k) => _curr.Contains(k);

    /// <summary>Returns true on the first frame the key was pressed.</summary>
    public static bool IsKeyJustPressed(Key k) => _curr.Contains(k) && !_prev.Contains(k);

    /// <summary>Returns true on the first frame the key was released.</summary>
    public static bool IsKeyJustReleased(Key k) => !_curr.Contains(k) && _prev.Contains(k);

    /// <summary>Returns the raw analog value [0,1] for a key (useful for triggers/axes).</summary>
    public static float GetKeyValue(Key k) => NativeApi.SA_Input_GetKey(KeyPath(k));

    /// <summary>Returns true while the given mouse button is held.</summary>
    public static bool IsMouseButtonDown(MouseButton btn) {
        string path = btn switch {
            MouseButton.Left   => "Mouse/Left",
            MouseButton.Right  => "Mouse/Right",
            MouseButton.Middle => "Mouse/Middle",
            _ => "Mouse/Left"
        };
        return NativeApi.SA_Input_GetKey(path) > 0.5f;
    }

    /// <summary>Returns the mouse movement delta this frame in pixels.</summary>
    public static Vector2 GetMouseDelta() {
        NativeApi.SA_Input_GetAxis2D("Mouse/Delta", out float x, out float y);
        return new Vector2(x, y);
    }
}

/// <summary>
/// Named-action input (Issue #71). Queries the engine InputSystem by action
/// name as configured via .sainputmap assets — decoupled from device paths.
/// </summary>
public static class InputAction
{
    /// <summary>Reads the current scalar value of an action (e.g. a trigger axis).</summary>
    public static float Read(string action) => NativeApi.SA_InputAction_ReadFloat(action);

    /// <summary>Reads the current 2D value of an action (e.g. a stick or composite XY).</summary>
    public static Vector2 ReadVec2(string action) {
        NativeApi.SA_InputAction_ReadVec2(action, out float x, out float y);
        return new Vector2(x, y);
    }

    /// <summary>True while the action is currently active.</summary>
    public static bool IsActive(string action)       => NativeApi.SA_InputAction_IsActive(action)       != 0;
    /// <summary>True on the first frame the action became active this frame.</summary>
    public static bool WasActivated(string action)   => NativeApi.SA_InputAction_WasActivated(action)   != 0;
    /// <summary>True on the first frame the action became inactive this frame.</summary>
    public static bool WasDeactivated(string action) => NativeApi.SA_InputAction_WasDeactivated(action) != 0;
}
