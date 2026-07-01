#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// Binary .refl format helpers
//
// The cook tool writes a .refl alongside every .spv using spirv-reflect.
// At runtime, ShaderProgram loads both files and populates ShaderReflection.
//
// File layout (little-endian):
//   magic              uint32_t  0x4C464552  ('REFL')
//   version            uint32_t  5
//   pushConstantSize   uint32_t
//   pushConstantStages uint32_t
//   bindingCount       uint32_t
//   bindings[N]:
//     set              uint32_t
//     binding          uint32_t
//     type             uint32_t  (RHIDescriptorType)
//     stages           uint32_t  (RHIShaderStage bits)
//     arraySize        uint32_t
//     blockSize        uint32_t  (declared struct size in bytes; 0 for non-buffer types)
//     memberCount      uint32_t  (> 0 only for UniformBuffer / StorageBuffer)
//     members[M]:
//       offset         uint32_t
//       size           uint32_t
//       uiType         uint8_t   (ParamUIType; v4+)
//       minValue       float     (v4+)
//       maxValue       float     (v4+)
//       defaultValue   float[4]  (v4+)
//       nameLen        uint32_t
//       name           char[nameLen]
//       displayNameLen uint32_t  (v4+)
//       displayName    char[displayNameLen]  (v4+)
//     nameLen          uint32_t
//     name             char[nameLen]  (no null terminator stored)
//     displayNameLen   uint32_t  (v4+)
//     displayName      char[displayNameLen]  (v4+)
//   --- v7+: generic metadata map (replaces v5 shadingModel/vertShader) ---
//   metadataCount      uint32_t  (v7+)
//   metadata[K]:                              (v7+)
//     keyLen           uint32_t
//     key              char[keyLen]
//     valLen           uint32_t
//     value            char[valLen]
//   (v5/v6 files instead store: shadingModelLen+str, vertShaderLen+str — migrated
//    into the metadata map on read under keys "shadingModel"/"vertShader")
//   vertexInputCount   uint32_t  (v6+; 0 for fragment/compute stages)
//   vertexInputs[V]:                          (v6+)
//     location         uint32_t
//     format           uint32_t  (RHIVertexFormat)
// ─────────────────────────────────────────────────────────────────────────────

namespace ShaderReflectionIO {

static constexpr uint32_t kMagic   = 0x4C464552u; // 'REFL'
static constexpr uint32_t kVersion = 7u;

// Serialize reflection to a byte buffer.
[[nodiscard]] std::vector<uint8_t> Serialize(const ShaderReflection& refl);

// Deserialize from raw bytes (e.g. mmap'd .refl file).
// Returns false and leaves 'out' untouched on failure.
[[nodiscard]] bool Deserialize(std::span<const uint8_t> data,
                               ShaderReflection&        out);

// Convenience: read a .refl file from disk.
[[nodiscard]] bool LoadFromFile(const std::filesystem::path& path,
                                ShaderReflection&             out);

// Convenience: write a .refl file to disk.
[[nodiscard]] bool SaveToFile(const std::filesystem::path& path,
                               const ShaderReflection&      refl);

} // namespace ShaderReflectionIO
} // namespace StellarAlia::RHI
