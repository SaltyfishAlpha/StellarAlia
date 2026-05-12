using System.Numerics;

namespace StellarAlia;

// Linear-space RGBA color (float channels, 0..1 typical but unclamped).
// Wire-compatible with Vector4: 4×f32 LE in the field-value blob.
public struct Color
{
    public float R, G, B, A;

    public Color(float r, float g, float b, float a = 1f) { R = r; G = g; B = b; A = a; }

    public static readonly Color White   = new(1f, 1f, 1f, 1f);
    public static readonly Color Black   = new(0f, 0f, 0f, 1f);
    public static readonly Color Red     = new(1f, 0f, 0f, 1f);
    public static readonly Color Green   = new(0f, 1f, 0f, 1f);
    public static readonly Color Blue    = new(0f, 0f, 1f, 1f);
    public static readonly Color Yellow  = new(1f, 1f, 0f, 1f);
    public static readonly Color Magenta = new(1f, 0f, 1f, 1f);
    public static readonly Color Cyan    = new(0f, 1f, 1f, 1f);
    public static readonly Color Clear   = new(0f, 0f, 0f, 0f);

    public Vector4 AsVector4() => new(R, G, B, A);

    public static implicit operator Vector4(Color c) => c.AsVector4();
    public static implicit operator Color(Vector4 v) => new(v.X, v.Y, v.Z, v.W);
}
