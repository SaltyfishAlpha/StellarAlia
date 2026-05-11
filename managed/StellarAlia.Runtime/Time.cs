namespace StellarAlia;

public static class Time
{
    public static float DeltaTime  => NativeApi.SA_Time_GetDeltaTime();
    public static float TotalTime  => NativeApi.SA_Time_GetTotalTime();
}
