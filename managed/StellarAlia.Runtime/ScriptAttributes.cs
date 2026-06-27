using System;

namespace StellarAlia;

// ─────────────────────────────────────────────────────────────────────────────
// Inspector attributes for script fields (Unity-style).
// FieldReflector reads these during schema build; the Inspector applies them
// in ScriptDrawer (Range → Slider; Tooltip → IsItemHovered; Header → SeparatorText;
// HideInInspector → skip render but keep in schema for serialization).
// ─────────────────────────────────────────────────────────────────────────────

/// <summary>Constrains a numeric field to a slider in the Inspector.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class RangeAttribute : Attribute
{
    /// <summary>Inclusive lower bound shown by the slider.</summary>
    public float Min;
    /// <summary>Inclusive upper bound shown by the slider.</summary>
    public float Max;
    /// <summary>Constructs the attribute with the given inclusive bounds.</summary>
    public RangeAttribute(float min, float max) { Min = min; Max = max; }
}

/// <summary>Sets a hover tooltip on a field in the Inspector.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class TooltipAttribute : Attribute
{
    /// <summary>Tooltip text shown when the user hovers the field.</summary>
    public string Text;
    /// <summary>Constructs the attribute with the given tooltip text.</summary>
    public TooltipAttribute(string text) { Text = text ?? string.Empty; }
}

/// <summary>Inserts a labelled section separator before the field in the Inspector.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class HeaderAttribute : Attribute
{
    /// <summary>Header label rendered above the field.</summary>
    public string Text;
    /// <summary>Constructs the attribute with the given header label.</summary>
    public HeaderAttribute(string text) { Text = text ?? string.Empty; }
}

/// <summary>Hides a field from the Inspector while still serializing it.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class HideInInspectorAttribute : Attribute { }

/// <summary>
/// Opt-in serialization for non-public fields. Reserved for the #75 plan:
/// when honoured the reflector will include <c>private</c> fields marked
/// with this attribute alongside the default <c>public</c> set.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class SerializeFieldAttribute : Attribute { }
