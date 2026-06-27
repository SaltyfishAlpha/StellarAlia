namespace StellarAlia;

/// <summary>Common math helpers wrapping <see cref="MathF"/>.</summary>
public static class Mathf
{
    /// <summary>π (3.14159…).</summary>
    public const float PI        = MathF.PI;
    /// <summary>Multiplier from degrees to radians.</summary>
    public const float Deg2Rad   = MathF.PI / 180f;
    /// <summary>Multiplier from radians to degrees.</summary>
    public const float Rad2Deg   = 180f / MathF.PI;
    /// <summary>Default tolerance used by <see cref="Approximately(float, float, float)"/>.</summary>
    public const float Epsilon   = 1e-5f;

    /// <summary>Linear interpolation (unclamped).</summary>
    public static float Lerp(float a, float b, float t) => a + (b - a) * t;

    /// <summary>Linear interpolation clamped to [0,1].</summary>
    public static float LerpClamped(float a, float b, float t) => Lerp(a, b, Clamp01(t));

    /// <summary>Clamps <paramref name="v"/> into the inclusive range [<paramref name="min"/>, <paramref name="max"/>].</summary>
    public static float Clamp(float v, float min, float max) => MathF.Max(min, MathF.Min(max, v));
    /// <summary>Clamps <paramref name="v"/> into the inclusive range [<paramref name="min"/>, <paramref name="max"/>].</summary>
    public static int   Clamp(int   v, int   min, int   max) => Math.Max(min, Math.Min(max, v));
    /// <summary>Clamps <paramref name="v"/> into [0, 1].</summary>
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

    /// <summary>Returns true when |a − b| ≤ eps (defaults to <see cref="Epsilon"/>).</summary>
    public static bool Approximately(float a, float b, float eps = Epsilon)
        => MathF.Abs(a - b) <= eps;

    /// <summary>Moves current towards target by at most maxDelta (handles negative delta).</summary>
    public static float MoveTowards(float current, float target, float maxDelta) {
        float diff = target - current;
        if (MathF.Abs(diff) <= MathF.Abs(maxDelta)) return target;
        return current + MathF.Sign(diff) * maxDelta;
    }

    /// <summary>Absolute value of <paramref name="v"/>.</summary>
    public static float Abs  (float v)          => MathF.Abs(v);
    /// <summary>Sign of <paramref name="v"/> (−1, 0, or 1).</summary>
    public static float Sign (float v)          => MathF.Sign(v);
    /// <summary>Square root of <paramref name="v"/>.</summary>
    public static float Sqrt (float v)          => MathF.Sqrt(v);
    /// <summary><paramref name="b"/> raised to the power <paramref name="e"/>.</summary>
    public static float Pow  (float b, float e) => MathF.Pow(b, e);
    /// <summary>Sine of an angle in radians.</summary>
    public static float Sin  (float r)          => MathF.Sin(r);
    /// <summary>Cosine of an angle in radians.</summary>
    public static float Cos  (float r)          => MathF.Cos(r);
    /// <summary>Tangent of an angle in radians.</summary>
    public static float Tan  (float r)          => MathF.Tan(r);
    /// <summary>Arc sine in radians.</summary>
    public static float Asin (float v)          => MathF.Asin(v);
    /// <summary>Arc cosine in radians.</summary>
    public static float Acos (float v)          => MathF.Acos(v);
    /// <summary>Two-argument arc tangent in radians.</summary>
    public static float Atan2(float y, float x) => MathF.Atan2(y, x);
    /// <summary>Largest integer ≤ <paramref name="v"/>.</summary>
    public static float Floor(float v)          => MathF.Floor(v);
    /// <summary>Smallest integer ≥ <paramref name="v"/>.</summary>
    public static float Ceil (float v)          => MathF.Ceiling(v);
    /// <summary>Rounds <paramref name="v"/> to the nearest integer (banker's rounding).</summary>
    public static float Round(float v)          => MathF.Round(v);
    /// <summary>Maximum of <paramref name="a"/> and <paramref name="b"/>.</summary>
    public static float Max  (float a, float b) => MathF.Max(a, b);
    /// <summary>Minimum of <paramref name="a"/> and <paramref name="b"/>.</summary>
    public static float Min  (float a, float b) => MathF.Min(a, b);
}
