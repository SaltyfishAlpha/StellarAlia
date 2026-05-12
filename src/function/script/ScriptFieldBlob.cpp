#include "function/script/ScriptFieldBlob.hpp"
#include "core/logs/Log.hpp"

#include <cstring>

namespace StellarAlia {

// ── BlobWriter ──────────────────────────────────────────────────────────────

void BlobWriter::WriteRaw(const void* data, size_t n) {
    const auto* bytes = static_cast<const std::byte*>(data);
    m_buf.insert(m_buf.end(), bytes, bytes + n);
}

void BlobWriter::WriteU8(uint8_t v)   { WriteRaw(&v, sizeof(v)); }
void BlobWriter::WriteU16(uint16_t v) { WriteRaw(&v, sizeof(v)); }
void BlobWriter::WriteU32(uint32_t v) { WriteRaw(&v, sizeof(v)); }
void BlobWriter::WriteI32(int32_t v)  { WriteRaw(&v, sizeof(v)); }
void BlobWriter::WriteF32(float v)    { WriteRaw(&v, sizeof(v)); }
void BlobWriter::WriteU64(uint64_t v) { WriteRaw(&v, sizeof(v)); }

void BlobWriter::WriteStr(std::string_view s) {
    size_t len = s.size();
    if (len > 65535) {
        SA_LOG_WARN("[ScriptFieldBlob] string truncated from {} to 65535 bytes", len);
        len = 65535;
    }
    WriteU16(static_cast<uint16_t>(len));
    WriteRaw(s.data(), len);
}

void BlobWriter::WriteUUID(const AssetID& id) {
    WriteU64(id.hi);
    WriteU64(id.lo);
}

// ── BlobReader ──────────────────────────────────────────────────────────────

bool BlobReader::ReadRaw(void* dst, size_t n) {
    if (m_bad || static_cast<size_t>(m_end - m_data) < n) { m_bad = true; return false; }
    std::memcpy(dst, m_data, n);
    m_data += n;
    return true;
}

bool BlobReader::Skip(size_t n) {
    if (m_bad || static_cast<size_t>(m_end - m_data) < n) { m_bad = true; return false; }
    m_data += n;
    return true;
}

bool BlobReader::ReadU8(uint8_t& out)   { return ReadRaw(&out, sizeof(out)); }
bool BlobReader::ReadU16(uint16_t& out) { return ReadRaw(&out, sizeof(out)); }
bool BlobReader::ReadU32(uint32_t& out) { return ReadRaw(&out, sizeof(out)); }
bool BlobReader::ReadI32(int32_t& out)  { return ReadRaw(&out, sizeof(out)); }
bool BlobReader::ReadF32(float& out)    { return ReadRaw(&out, sizeof(out)); }
bool BlobReader::ReadU64(uint64_t& out) { return ReadRaw(&out, sizeof(out)); }

bool BlobReader::ReadStr(std::string& out) {
    uint16_t len = 0;
    if (!ReadU16(len)) return false;
    out.assign(len, '\0');
    return ReadRaw(out.data(), len);
}

bool BlobReader::ReadUUID(AssetID& out) {
    return ReadU64(out.hi) && ReadU64(out.lo);
}

// ── EncodeFieldValues ───────────────────────────────────────────────────────

namespace {

void WriteValuePayload(BlobWriter& w, ScriptFieldKind kind, const ScriptFieldValue& v) {
    switch (kind) {
        case ScriptFieldKind::Bool: {
            const bool* p = std::get_if<bool>(&v);
            w.WriteU8(p && *p ? 1 : 0);
            break;
        }
        case ScriptFieldKind::Int32:
        case ScriptFieldKind::Enum: {
            const int32_t* p = std::get_if<int32_t>(&v);
            w.WriteI32(p ? *p : 0);
            break;
        }
        case ScriptFieldKind::Float: {
            const float* p = std::get_if<float>(&v);
            w.WriteF32(p ? *p : 0.f);
            break;
        }
        case ScriptFieldKind::Vec2: {
            const glm::vec2* p = std::get_if<glm::vec2>(&v);
            glm::vec2 z{}; const auto& vec = p ? *p : z;
            w.WriteF32(vec.x); w.WriteF32(vec.y);
            break;
        }
        case ScriptFieldKind::Vec3:
        case ScriptFieldKind::Color: {
            // Color can hold vec3 OR vec4; native value variant decides.
            if (const glm::vec3* p3 = std::get_if<glm::vec3>(&v)) {
                w.WriteF32(p3->x); w.WriteF32(p3->y); w.WriteF32(p3->z);
            } else if (const glm::vec4* p4 = std::get_if<glm::vec4>(&v)) {
                w.WriteF32(p4->x); w.WriteF32(p4->y); w.WriteF32(p4->z); w.WriteF32(p4->w);
            } else {
                w.WriteF32(0); w.WriteF32(0); w.WriteF32(0);
            }
            break;
        }
        case ScriptFieldKind::Vec4: {
            const glm::vec4* p = std::get_if<glm::vec4>(&v);
            glm::vec4 z{}; const auto& vec = p ? *p : z;
            w.WriteF32(vec.x); w.WriteF32(vec.y); w.WriteF32(vec.z); w.WriteF32(vec.w);
            break;
        }
        case ScriptFieldKind::String: {
            const std::string* p = std::get_if<std::string>(&v);
            w.WriteStr(p ? *p : std::string{});
            break;
        }
        case ScriptFieldKind::AssetRef: {
            const AssetID* p = std::get_if<AssetID>(&v);
            AssetID z; w.WriteUUID(p ? *p : z);
            break;
        }
        case ScriptFieldKind::EntityRef: {
            const uint64_t* p = std::get_if<uint64_t>(&v);
            w.WriteU64(p ? *p : 0ull);
            break;
        }
        case ScriptFieldKind::Unsupported:
        default:
            break;
    }
}

// Pick the ScriptFieldKind tag from a ScriptFieldValue when the caller does NOT
// have a schema to consult (used by EncodeFieldValues which only sees value).
// Heuristic: variant alternative → minimal kind (Color folded into Vec3/Vec4 by
// the actual variant contents; Enum collapses to Int32; EntityRef appears as
// raw uint64).
ScriptFieldKind KindFromValue(const ScriptFieldValue& v) {
    return std::visit([](auto&& x) -> ScriptFieldKind {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, bool>)        return ScriptFieldKind::Bool;
        else if constexpr (std::is_same_v<T, int32_t>) return ScriptFieldKind::Int32;
        else if constexpr (std::is_same_v<T, float>)   return ScriptFieldKind::Float;
        else if constexpr (std::is_same_v<T, std::string>) return ScriptFieldKind::String;
        else if constexpr (std::is_same_v<T, glm::vec2>)   return ScriptFieldKind::Vec2;
        else if constexpr (std::is_same_v<T, glm::vec3>)   return ScriptFieldKind::Vec3;
        else if constexpr (std::is_same_v<T, glm::vec4>)   return ScriptFieldKind::Vec4;
        else if constexpr (std::is_same_v<T, AssetID>)     return ScriptFieldKind::AssetRef;
        else if constexpr (std::is_same_v<T, uint64_t>)    return ScriptFieldKind::EntityRef;
        else return ScriptFieldKind::Unsupported;
    }, v);
}

uint16_t PayloadSizeFor(ScriptFieldKind kind, const ScriptFieldValue& v) {
    switch (kind) {
        case ScriptFieldKind::Bool: return 1;
        case ScriptFieldKind::Int32:
        case ScriptFieldKind::Enum:
        case ScriptFieldKind::Float: return 4;
        case ScriptFieldKind::Vec2: return 8;
        case ScriptFieldKind::Vec3: return 12;
        case ScriptFieldKind::Vec4: return 16;
        case ScriptFieldKind::Color: {
            // Variable: vec3 (12) or vec4 (16) depending on which variant alt is active.
            if (std::holds_alternative<glm::vec4>(v)) return 16;
            return 12;
        }
        case ScriptFieldKind::AssetRef:  return 16;
        case ScriptFieldKind::EntityRef: return 8;
        case ScriptFieldKind::String: {
            const auto* p = std::get_if<std::string>(&v);
            const size_t bodyLen = p ? std::min<size_t>(p->size(), 65535) : 0;
            return static_cast<uint16_t>(2u + bodyLen);   // u16 len + bytes
        }
        default: return 0;
    }
}

void EncodeRecord(BlobWriter& w, std::string_view name, const ScriptFieldValue& v) {
    const ScriptFieldKind kind   = KindFromValue(v);
    const uint16_t        payLen = PayloadSizeFor(kind, v);
    w.WriteStr(name);
    w.WriteU8(static_cast<uint8_t>(kind));
    w.WriteU16(payLen);
    WriteValuePayload(w, kind, v);
}

} // namespace

void EncodeFieldValues(
    const std::unordered_map<std::string, ScriptFieldValue>& fields,
    std::vector<std::byte>& out)
{
    BlobWriter w;
    w.WriteU32(static_cast<uint32_t>(fields.size()));
    for (const auto& [name, value] : fields)
        EncodeRecord(w, name, value);
    out = std::move(w.MoveData());
}

void EncodeSingleField(std::string_view name,
                       const ScriptFieldValue& value,
                       std::vector<std::byte>& out)
{
    BlobWriter w;
    w.WriteU32(1u);
    EncodeRecord(w, name, value);
    out = std::move(w.MoveData());
}

// ── DecodeFieldValues ───────────────────────────────────────────────────────

namespace {

// Returns true on success. On schema mismatch (payloadLen ≠ expected for kind)
// or unknown kind, skips the payload bytes and returns false to signal "field
// dropped, continue with next record".
bool DecodeRecordPayload(BlobReader& r, ScriptFieldKind kind, uint16_t payloadLen,
                         ScriptFieldValue& out)
{
    switch (kind) {
        case ScriptFieldKind::Bool: {
            if (payloadLen != 1) { r.Skip(payloadLen); return false; }
            uint8_t b = 0;
            if (!r.ReadU8(b)) return false;
            out = (b != 0);
            return true;
        }
        case ScriptFieldKind::Int32:
        case ScriptFieldKind::Enum: {
            if (payloadLen != 4) { r.Skip(payloadLen); return false; }
            int32_t v = 0;
            if (!r.ReadI32(v)) return false;
            out = v;
            return true;
        }
        case ScriptFieldKind::Float: {
            if (payloadLen != 4) { r.Skip(payloadLen); return false; }
            float v = 0;
            if (!r.ReadF32(v)) return false;
            out = v;
            return true;
        }
        case ScriptFieldKind::Vec2: {
            if (payloadLen != 8) { r.Skip(payloadLen); return false; }
            glm::vec2 v{};
            if (!r.ReadF32(v.x) || !r.ReadF32(v.y)) return false;
            out = v;
            return true;
        }
        case ScriptFieldKind::Vec3: {
            if (payloadLen != 12) { r.Skip(payloadLen); return false; }
            glm::vec3 v{};
            if (!r.ReadF32(v.x) || !r.ReadF32(v.y) || !r.ReadF32(v.z)) return false;
            out = v;
            return true;
        }
        case ScriptFieldKind::Vec4: {
            if (payloadLen != 16) { r.Skip(payloadLen); return false; }
            glm::vec4 v{};
            if (!r.ReadF32(v.x) || !r.ReadF32(v.y) || !r.ReadF32(v.z) || !r.ReadF32(v.w)) return false;
            out = v;
            return true;
        }
        case ScriptFieldKind::Color: {
            if (payloadLen == 12) {
                glm::vec3 v{};
                if (!r.ReadF32(v.x) || !r.ReadF32(v.y) || !r.ReadF32(v.z)) return false;
                out = v;
            } else if (payloadLen == 16) {
                glm::vec4 v{};
                if (!r.ReadF32(v.x) || !r.ReadF32(v.y) || !r.ReadF32(v.z) || !r.ReadF32(v.w)) return false;
                out = v;
            } else {
                r.Skip(payloadLen);
                return false;
            }
            return true;
        }
        case ScriptFieldKind::String: {
            std::string s;
            if (!r.ReadStr(s)) return false;
            out = std::move(s);
            return true;
        }
        case ScriptFieldKind::AssetRef: {
            if (payloadLen != 16) { r.Skip(payloadLen); return false; }
            AssetID id{};
            if (!r.ReadUUID(id)) return false;
            out = id;
            return true;
        }
        case ScriptFieldKind::EntityRef: {
            if (payloadLen != 8) { r.Skip(payloadLen); return false; }
            uint64_t v = 0;
            if (!r.ReadU64(v)) return false;
            out = v;
            return true;
        }
        default: {
            r.Skip(payloadLen);
            return false;
        }
    }
}

} // namespace

bool DecodeFieldValues(const std::byte* data, size_t size,
                       std::unordered_map<std::string, ScriptFieldValue>& out)
{
    BlobReader r(data, size);
    uint32_t count = 0;
    if (!r.ReadU32(count)) return false;
    out.reserve(count);

    for (uint32_t i = 0; i < count && !r.Bad(); ++i) {
        std::string name;
        uint8_t     kindByte = 0;
        uint16_t    payLen   = 0;
        if (!r.ReadStr(name) || !r.ReadU8(kindByte) || !r.ReadU16(payLen)) return false;

        ScriptFieldValue value;
        const ScriptFieldKind kind = static_cast<ScriptFieldKind>(kindByte);
        if (DecodeRecordPayload(r, kind, payLen, value))
            out.emplace(std::move(name), std::move(value));
        // else: record skipped (already advanced past payload via Skip).
    }
    return !r.Bad();
}

// ── DecodeSchema ────────────────────────────────────────────────────────────

bool DecodeSchema(const std::byte* data, size_t size, ScriptClassSchema& out) {
    BlobReader r(data, size);

    uint16_t wireVersion = 0;
    if (!r.ReadU16(wireVersion)) return false;
    if (wireVersion == 0) return false;

    if (!r.ReadStr(out.className)) return false;

    uint32_t fieldCount = 0;
    if (!r.ReadU32(fieldCount)) return false;
    out.fields.clear();
    out.fields.reserve(fieldCount);

    for (uint32_t i = 0; i < fieldCount && !r.Bad(); ++i) {
        ScriptFieldDescriptor f;
        uint8_t kindByte = 0;
        if (!r.ReadStr(f.name)) return false;
        if (!r.ReadU8(kindByte)) return false;
        f.kind = static_cast<ScriptFieldKind>(kindByte);
        if (!r.ReadStr(f.typeHint)) return false;
        if (!r.ReadU16(f.byteSize)) return false;

        // v2 attribute trailer: tooltip / header / flags / range. Missing in v1.
        if (wireVersion >= 2) {
            if (!r.ReadStr(f.tooltip)) return false;
            if (!r.ReadStr(f.header))  return false;
            uint8_t flags = 0;
            if (!r.ReadU8(flags)) return false;
            f.hidden   = (flags & 0x01) != 0;
            f.hasRange = (flags & 0x02) != 0;
            if (f.hasRange) {
                if (!r.ReadF32(f.rangeMin)) return false;
                if (!r.ReadF32(f.rangeMax)) return false;
            }
        }
        if (f.label.empty()) f.label = f.name;
        out.fields.push_back(std::move(f));
    }

    return !r.Bad();
}

} // namespace StellarAlia
