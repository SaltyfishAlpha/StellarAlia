#pragma once

#include "function/script/ScriptFieldSchema.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// Wire format — all little-endian, no padding.
//
// Primitive types (shared by both blobs):
//   u8 / u16 / u32 / i32 / f32 — raw LE bytes
//   str  : u16 len + utf8[len]      (len capped at 65535)
//   uuid : 16 bytes (hi LE, lo LE)
//
// Schema blob (GetClassSchemaBlob → DecodeSchema):
//   u16   schemaVersion          // 1 in #74
//   str   className
//   u32   fieldCount
//   field fields[fieldCount]
//
//   field_v1 (schemaVersion=1):
//     str  name
//     u8   kind
//     str  typeHint
//     u16  byteSize
//   Future versions extend field tail only; reader skips unknown trailing bytes.
//
// Field-value blob (ApplyFieldValues / CaptureFieldValues):
//   u32   recordCount
//   record records[recordCount]
//
//   record:
//     str  name
//     u8   kind
//     u16  payloadLen
//     byte payload[payloadLen]
// ─────────────────────────────────────────────────────────────────────────────

// Wire version of the schema blob. v1 = #74 (name/kind/typeHint/byteSize);
// v2 = #75 (+ tooltip/header/hidden/rangeMin/rangeMax). Reader is back-compat:
// v1 input loads with default attribute values.
constexpr uint16_t kScriptSchemaWireVersion = 2;

class BlobWriter {
public:
    void WriteU8 (uint8_t  v);
    void WriteU16(uint16_t v);
    void WriteU32(uint32_t v);
    void WriteI32(int32_t  v);
    void WriteF32(float    v);
    void WriteU64(uint64_t v);
    // u16 length-prefixed utf8 (max 65535 bytes; truncates and warns past that).
    void WriteStr (std::string_view s);
    void WriteUUID(const AssetID& id);
    void WriteRaw (const void* data, size_t n);

    const std::vector<std::byte>& Data() const { return m_buf; }
    std::vector<std::byte>&&      MoveData()    { return std::move(m_buf); }
    size_t                        Size() const  { return m_buf.size(); }

private:
    std::vector<std::byte> m_buf;
};

class BlobReader {
public:
    BlobReader(const std::byte* data, size_t size) : m_data(data), m_end(data + size) {}

    bool ReadU8 (uint8_t&  out);
    bool ReadU16(uint16_t& out);
    bool ReadU32(uint32_t& out);
    bool ReadI32(int32_t&  out);
    bool ReadF32(float&    out);
    bool ReadU64(uint64_t& out);
    bool ReadStr (std::string& out);
    bool ReadUUID(AssetID& out);
    bool ReadRaw (void* dst, size_t n);
    // Advance past n bytes without reading (used to skip trailing fields from a
    // newer schema version).
    bool Skip(size_t n);

    bool   Eof()  const { return m_data == m_end; }
    bool   Bad()  const { return m_bad; }
    size_t Remaining() const { return static_cast<size_t>(m_end - m_data); }

private:
    const std::byte* m_data;
    const std::byte* m_end;
    bool             m_bad = false;
};

// ── Schema blob ──────────────────────────────────────────────────────────────
//
// Version-aware: reads schemaVersion first, then field records using whatever
// fields the current build understands; extra trailing bytes per field from a
// future version are skipped.
bool DecodeSchema(const std::byte* data, size_t size, ScriptClassSchema& out);

// ── Field-value blob ─────────────────────────────────────────────────────────

void EncodeFieldValues(
    const std::unordered_map<std::string, ScriptFieldValue>& fields,
    std::vector<std::byte>& out);

bool DecodeFieldValues(
    const std::byte* data, size_t size,
    std::unordered_map<std::string, ScriptFieldValue>& out);

// Encode a single field record (used for delta inject in Step 11).
void EncodeSingleField(std::string_view name,
                       const ScriptFieldValue& value,
                       std::vector<std::byte>& out);

} // namespace StellarAlia
