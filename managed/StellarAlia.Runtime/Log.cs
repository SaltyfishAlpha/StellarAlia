namespace StellarAlia;

/// <summary>Shorthand for engine-console logging. See also <see cref="Debug"/>.</summary>
public static class Log
{
    /// <summary>Writes an informational message to the engine console.</summary>
    public static void Info (string msg) => NativeApi.SA_Log_Info(msg);
    /// <summary>Writes a warning message to the engine console.</summary>
    public static void Warn (string msg) => NativeApi.SA_Log_Warn(msg);
    /// <summary>Writes an error message to the engine console.</summary>
    public static void Error(string msg) => NativeApi.SA_Log_Error(msg);
}
