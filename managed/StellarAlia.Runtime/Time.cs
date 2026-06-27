namespace StellarAlia;

/// <summary>Frame and total-elapsed time queries.</summary>
public static class Time
{
    /// <summary>Seconds elapsed since the previous frame.</summary>
    public static float DeltaTime  => NativeApi.SA_Time_GetDeltaTime();
    /// <summary>Seconds elapsed since the engine started (or scene loaded, depending on context).</summary>
    public static float TotalTime  => NativeApi.SA_Time_GetTotalTime();
}
