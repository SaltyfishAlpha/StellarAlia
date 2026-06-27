using System.Numerics;

namespace StellarAlia;

/// <summary>
/// Script-side logging and debug-draw utilities. Output is routed to the
/// engine console; debug lines render in the editor viewport for one frame.
/// </summary>
public static class Debug
{
    private static readonly Vector4 White = new(1, 1, 1, 1);

    /// <summary>Writes an informational message to the engine console.</summary>
    public static void Log(string msg)   => NativeApi.SA_Log_Info(msg);
    /// <summary>Writes a warning message to the engine console.</summary>
    public static void Warn(string msg)  => NativeApi.SA_Log_Warn(msg);
    /// <summary>Writes an error message to the engine console.</summary>
    public static void Error(string msg) => NativeApi.SA_Log_Error(msg);

    /// <summary>Draws a one-frame line in world space with the given RGBA color.</summary>
    public static void DrawLine(Vector3 from, Vector3 to, Vector4 color) =>
        NativeApi.SA_Debug_DrawLine(from.X, from.Y, from.Z,
                                    to.X,   to.Y,   to.Z,
                                    color.X, color.Y, color.Z, color.W);

    /// <summary>Draws a one-frame white line in world space.</summary>
    public static void DrawLine(Vector3 from, Vector3 to) =>
        DrawLine(from, to, White);
}
