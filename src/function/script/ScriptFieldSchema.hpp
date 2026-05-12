#pragma once

#include "core/asset/AssetID.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ScriptFieldKind — wire-stable enum shared with managed FieldReflector via the
// schema-blob and field-value-blob formats.  Values are explicit and must NOT
// change once shipped (binary blob compatibility).
// ─────────────────────────────────────────────────────────────────────────────
enum class ScriptFieldKind : uint8_t {
    Bool        = 0,
    Int32       = 1,
    Float       = 2,
    String      = 3,
    Vec2        = 4,
    Vec3        = 5,
    Vec4        = 6,
    // Read-only in #74; editor support lands in #75.
    AssetRef    = 16,
    EntityRef   = 17,
    Color       = 18,
    Enum        = 19,
    Unsupported = 255,
};

// One field's schema entry (matches the field_v* layout in schema blob).
struct ScriptFieldDescriptor {
    std::string     name;       // C# field name
    std::string     label;      // defaults to name
    ScriptFieldKind kind     = ScriptFieldKind::Unsupported;
    std::string     typeHint; // AssetRef → "Mesh"/"Texture"; Enum → C# FQN; else ""
    uint16_t        byteSize = 0;  // fixed-size payload in field-value blob; 0 for variable-length kinds

    // #75 attribute data (schema v2). Reader treats missing values as defaults.
    std::string     tooltip;        // empty = no tooltip
    std::string     header;         // non-empty = emit SeparatorText before field
    bool            hidden   = false; // [HideInInspector] — schema includes field for serialization only
    float           rangeMin = 0.f;   // [Range(min,max)] → SliderInt/SliderFloat
    float           rangeMax = 0.f;
    bool            hasRange = false;
};

// Native-side value container; one entry per field on a ScriptComponent.
//
// One std::variant alternative may represent multiple wire kinds (the kind tag
// on the schema disambiguates: e.g. Int32 and Enum both use int32_t; Color uses
// vec3 or vec4 depending on the C# field type).
using ScriptFieldValue = std::variant<
    bool,           // Bool
    int32_t,        // Int32, Enum
    float,          // Float
    std::string,    // String
    glm::vec2,      // Vec2
    glm::vec3,      // Vec3, Color (3-component)
    glm::vec4,      // Vec4, Color (4-component)
    AssetID,        // AssetRef
    uint64_t        // EntityRef (sceneLocalId in #75)
>;

struct ScriptClassSchema {
    std::string                          className;   // C# FQN
    std::vector<ScriptFieldDescriptor>   fields;
    // Field initializers captured from `Activator.CreateInstance(type)`.
    // Populated alongside the schema by ScriptSystem (GetClassDefaultsBlob)
    // so the Inspector can seed sc.fields with the C# `= value` initializers
    // when the user adds a script or hot-recompile introduces new fields.
    std::unordered_map<std::string, ScriptFieldValue> defaults;
};

// Stable string names used in .sascene `fields[].kind`. Wire-compatible with
// the binary kind byte but kept as enum text for human-readable JSON.
inline const char* ScriptFieldKindToString(ScriptFieldKind k) {
    switch (k) {
        case ScriptFieldKind::Bool:        return "Bool";
        case ScriptFieldKind::Int32:       return "Int32";
        case ScriptFieldKind::Float:       return "Float";
        case ScriptFieldKind::String:      return "String";
        case ScriptFieldKind::Vec2:        return "Vec2";
        case ScriptFieldKind::Vec3:        return "Vec3";
        case ScriptFieldKind::Vec4:        return "Vec4";
        case ScriptFieldKind::AssetRef:    return "AssetRef";
        case ScriptFieldKind::EntityRef:   return "EntityRef";
        case ScriptFieldKind::Color:       return "Color";
        case ScriptFieldKind::Enum:        return "Enum";
        case ScriptFieldKind::Unsupported: return "Unsupported";
    }
    return "Unsupported";
}

inline ScriptFieldKind ScriptFieldKindFromString(std::string_view s) {
    if (s == "Bool")        return ScriptFieldKind::Bool;
    if (s == "Int32")       return ScriptFieldKind::Int32;
    if (s == "Float")       return ScriptFieldKind::Float;
    if (s == "String")      return ScriptFieldKind::String;
    if (s == "Vec2")        return ScriptFieldKind::Vec2;
    if (s == "Vec3")        return ScriptFieldKind::Vec3;
    if (s == "Vec4")        return ScriptFieldKind::Vec4;
    if (s == "AssetRef")    return ScriptFieldKind::AssetRef;
    if (s == "EntityRef")   return ScriptFieldKind::EntityRef;
    if (s == "Color")       return ScriptFieldKind::Color;
    if (s == "Enum")        return ScriptFieldKind::Enum;
    return ScriptFieldKind::Unsupported;
}

} // namespace StellarAlia
