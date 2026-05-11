using System.Numerics;

namespace StellarAlia;

public static class Debug
{
    private static readonly Vector4 White = new(1, 1, 1, 1);

    public static void Log(string msg)   => NativeApi.SA_Log_Info(msg);
    public static void Warn(string msg)  => NativeApi.SA_Log_Warn(msg);
    public static void Error(string msg) => NativeApi.SA_Log_Error(msg);

    public static void DrawLine(Vector3 from, Vector3 to, Vector4 color) =>
        NativeApi.SA_Debug_DrawLine(from.X, from.Y, from.Z,
                                    to.X,   to.Y,   to.Z,
                                    color.X, color.Y, color.Z, color.W);

    public static void DrawLine(Vector3 from, Vector3 to) =>
        DrawLine(from, to, White);
}
