namespace StellarAlia;

public static class Log
{
    public static void Info (string msg) => NativeApi.SA_Log_Info(msg);
    public static void Warn (string msg) => NativeApi.SA_Log_Warn(msg);
    public static void Error(string msg) => NativeApi.SA_Log_Error(msg);
}
