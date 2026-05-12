using System;

namespace StellarAlia;

// ─────────────────────────────────────────────────────────────────────────────
// Inspector attributes for script fields (Unity-style).
// FieldReflector reads these during schema build; the Inspector applies them
// in ScriptDrawer (Range → Slider; Tooltip → IsItemHovered; Header → SeparatorText;
// HideInInspector → skip render but keep in schema for serialization).
// ─────────────────────────────────────────────────────────────────────────────

[AttributeUsage(AttributeTargets.Field)]
public sealed class RangeAttribute : Attribute
{
    public float Min, Max;
    public RangeAttribute(float min, float max) { Min = min; Max = max; }
}

[AttributeUsage(AttributeTargets.Field)]
public sealed class TooltipAttribute : Attribute
{
    public string Text;
    public TooltipAttribute(string text) { Text = text ?? string.Empty; }
}

[AttributeUsage(AttributeTargets.Field)]
public sealed class HeaderAttribute : Attribute
{
    public string Text;
    public HeaderAttribute(string text) { Text = text ?? string.Empty; }
}

[AttributeUsage(AttributeTargets.Field)]
public sealed class HideInInspectorAttribute : Attribute { }

// Reserved for the #75 plan: opt-in for private fields. Currently
// FieldReflector only scans `public` — when this attribute is honoured the
// reflector will also include `private` fields marked with [SerializeField].
[AttributeUsage(AttributeTargets.Field)]
public sealed class SerializeFieldAttribute : Attribute { }
