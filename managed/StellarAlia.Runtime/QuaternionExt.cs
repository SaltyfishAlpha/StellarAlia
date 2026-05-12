using System.Numerics;

namespace StellarAlia;

/// <summary>Extension and factory helpers for <see cref="Quaternion"/>.</summary>
public static class QuaternionExt
{
    /// <summary>Creates a quaternion from Euler angles in degrees (XYZ order, same as SetRotationEuler).</summary>
    public static Quaternion FromEulerDegrees(float x, float y, float z) =>
        FromEulerRadians(x * Mathf.Deg2Rad, y * Mathf.Deg2Rad, z * Mathf.Deg2Rad);

    /// <summary>Creates a quaternion from Euler angles in degrees as a Vector3.</summary>
    public static Quaternion FromEulerDegrees(Vector3 degrees) =>
        FromEulerDegrees(degrees.X, degrees.Y, degrees.Z);

    /// <summary>Creates a quaternion from Euler angles in radians (XYZ order).</summary>
    public static Quaternion FromEulerRadians(float x, float y, float z) =>
        Quaternion.CreateFromYawPitchRoll(y, x, z);

    /// <summary>Spherical linear interpolation between two quaternions.</summary>
    public static Quaternion Slerp(Quaternion a, Quaternion b, float t) =>
        Quaternion.Slerp(a, b, t);

    /// <summary>Rotates <paramref name="q"/> toward <paramref name="target"/> by at most <paramref name="maxDegrees"/> per call.</summary>
    public static Quaternion RotateTowards(Quaternion q, Quaternion target, float maxDegrees) {
        float angle = AngleDegrees(q, target);
        if (angle < Mathf.Epsilon) return target;
        float t = MathF.Min(1f, maxDegrees / angle);
        return Quaternion.Slerp(q, target, t);
    }

    /// <summary>Angle in degrees between two quaternions.</summary>
    public static float AngleDegrees(Quaternion a, Quaternion b) {
        float dot = MathF.Abs(Quaternion.Dot(a, b));
        return dot > 1f - 1e-6f ? 0f : MathF.Acos(MathF.Min(dot, 1f)) * 2f * Mathf.Rad2Deg;
    }

    /// <summary>Extracts Euler angles in degrees (XYZ, matching glm eulerAngles convention).</summary>
    public static Vector3 ToEulerDegrees(Quaternion q) {
        // pitch (X), yaw (Y), roll (Z) — convert via matrix extraction
        float sinr_cosp = 2f * (q.W * q.X + q.Y * q.Z);
        float cosr_cosp = 1f - 2f * (q.X * q.X + q.Y * q.Y);
        float pitch = MathF.Atan2(sinr_cosp, cosr_cosp) * Mathf.Rad2Deg;

        float sinp = 2f * (q.W * q.Y - q.Z * q.X);
        float yaw  = MathF.Abs(sinp) >= 1f
            ? MathF.CopySign(90f, sinp)
            : MathF.Asin(sinp) * Mathf.Rad2Deg;

        float siny_cosp = 2f * (q.W * q.Z + q.X * q.Y);
        float cosy_cosp = 1f - 2f * (q.Y * q.Y + q.Z * q.Z);
        float roll = MathF.Atan2(siny_cosp, cosy_cosp) * Mathf.Rad2Deg;

        return new Vector3(pitch, yaw, roll);
    }
}
