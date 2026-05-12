namespace StellarAlia;

/// <summary>Common math helpers wrapping <see cref="MathF"/>.</summary>
public static class Mathf
{
    public const float PI        = MathF.PI;
    public const float Deg2Rad   = MathF.PI / 180f;
    public const float Rad2Deg   = 180f / MathF.PI;
    public const float Epsilon   = 1e-5f;

    /// <summary>Linear interpolation (unclamped).</summary>
    public static float Lerp(float a, float b, float t) => a + (b - a) * t;

    /// <summary>Linear interpolation clamped to [0,1].</summary>
    public static float LerpClamped(float a, float b, float t) => Lerp(a, b, Clamp01(t));

    public static float Clamp(float v, float min, float max) => MathF.Max(min, MathF.Min(max, v));
    public static int   Clamp(int   v, int   min, int   max) => Math.Max(min, Math.Min(max, v));
    public static float Clamp01(float v) => Clamp(v, 0f, 1f);

    /// <summary>Ping-pongs t in [0, length].</summary>
    public static float PingPong(float t, float length) {
        if (length == 0f) return 0f;
        float norm = t / length;
        float mod  = norm - MathF.Floor(norm);
        return (MathF.Floor(norm) % 2 == 0 ? mod : 1f - mod) * length;
    }

    /// <summary>Hermite smooth step in [a,b] at edge0=a, edge1=b.</summary>
    public static float SmoothStep(float a, float b, float t) {
        t = Clamp01((t - a) / (b - a));
        return t * t * (3f - 2f * t);
    }

    public static bool Approximately(float a, float b, float eps = Epsilon)
        => MathF.Abs(a - b) <= eps;

    /// <summary>Moves current towards target by at most maxDelta (handles negative delta).</summary>
    public static float MoveTowards(float current, float target, float maxDelta) {
        float diff = target - current;
        if (MathF.Abs(diff) <= MathF.Abs(maxDelta)) return target;
        return current + MathF.Sign(diff) * maxDelta;
    }

    public static float Abs  (float v)          => MathF.Abs(v);
    public static float Sign (float v)          => MathF.Sign(v);
    public static float Sqrt (float v)          => MathF.Sqrt(v);
    public static float Pow  (float b, float e) => MathF.Pow(b, e);
    public static float Sin  (float r)          => MathF.Sin(r);
    public static float Cos  (float r)          => MathF.Cos(r);
    public static float Tan  (float r)          => MathF.Tan(r);
    public static float Asin (float v)          => MathF.Asin(v);
    public static float Acos (float v)          => MathF.Acos(v);
    public static float Atan2(float y, float x) => MathF.Atan2(y, x);
    public static float Floor(float v)          => MathF.Floor(v);
    public static float Ceil (float v)          => MathF.Ceiling(v);
    public static float Round(float v)          => MathF.Round(v);
    public static float Max  (float a, float b) => MathF.Max(a, b);
    public static float Min  (float a, float b) => MathF.Min(a, b);
}
