using System.Numerics;

namespace StellarAlia;

/// <summary>
/// Linear-space RGBA color (float channels, 0..1 typical but unclamped).
/// Wire-compatible with <see cref="Vector4"/>: 4×f32 LE in the field-value blob.
/// </summary>
public struct Color
{
    /// <summary>Red channel (linear, 0..1 typical).</summary>
    public float R;
    /// <summary>Green channel (linear, 0..1 typical).</summary>
    public float G;
    /// <summary>Blue channel (linear, 0..1 typical).</summary>
    public float B;
    /// <summary>Alpha channel (0 = transparent, 1 = opaque).</summary>
    public float A;

    /// <summary>Constructs a color from RGBA channels. Alpha defaults to 1 (opaque).</summary>
    public Color(float r, float g, float b, float a = 1f) { R = r; G = g; B = b; A = a; }

    /// <summary>Opaque white (1, 1, 1, 1).</summary>
    public static readonly Color White   = new(1f, 1f, 1f, 1f);
    /// <summary>Opaque black (0, 0, 0, 1).</summary>
    public static readonly Color Black   = new(0f, 0f, 0f, 1f);
    /// <summary>Opaque red (1, 0, 0, 1).</summary>
    public static readonly Color Red     = new(1f, 0f, 0f, 1f);
    /// <summary>Opaque green (0, 1, 0, 1).</summary>
    public static readonly Color Green   = new(0f, 1f, 0f, 1f);
    /// <summary>Opaque blue (0, 0, 1, 1).</summary>
    public static readonly Color Blue    = new(0f, 0f, 1f, 1f);
    /// <summary>Opaque yellow (1, 1, 0, 1).</summary>
    public static readonly Color Yellow  = new(1f, 1f, 0f, 1f);
    /// <summary>Opaque magenta (1, 0, 1, 1).</summary>
    public static readonly Color Magenta = new(1f, 0f, 1f, 1f);
    /// <summary>Opaque cyan (0, 1, 1, 1).</summary>
    public static readonly Color Cyan    = new(0f, 1f, 1f, 1f);
    /// <summary>Fully transparent black (0, 0, 0, 0).</summary>
    public static readonly Color Clear   = new(0f, 0f, 0f, 0f);

    /// <summary>Returns this color as a <see cref="Vector4"/> (R, G, B, A).</summary>
    public Vector4 AsVector4() => new(R, G, B, A);

    /// <summary>Implicit conversion to <see cref="Vector4"/>.</summary>
    public static implicit operator Vector4(Color c) => c.AsVector4();
    /// <summary>Implicit conversion from <see cref="Vector4"/> (X→R, Y→G, Z→B, W→A).</summary>
    public static implicit operator Color(Vector4 v) => new(v.X, v.Y, v.Z, v.W);
}
