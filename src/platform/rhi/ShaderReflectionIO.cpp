#include "platform/rhi/ShaderReflectionIO.hpp"
#include "core/logs/Log.hpp"

#include <cstring>
#include <fstream>

namespace StellarAlia::RHI::ShaderReflectionIO {

// ─── helpers ─────────────────────────────────────────────────────────────────

namespace {

void WriteU32(std::vector<uint8_t>& buf, uint32_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    buf.insert(buf.end(), p, p + 4);
}

void WriteU8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

void WriteFloat(std::vector<uint8_t>& buf, float v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    buf.insert(buf.end(), p, p + 4);
}

bool ReadU32(std::span<const uint8_t> data, size_t& offset, uint32_t& out) {
    if (offset + 4 > data.size()) return false;
    std::memcpy(&out, data.data() + offset, 4);
    offset += 4;
    return true;
}

bool ReadU8(std::span<const uint8_t> data, size_t& offset, uint8_t& out) {
    if (offset + 1 > data.size()) return false;
    out = data[offset++];
    return true;
}

bool ReadFloat(std::span<const uint8_t> data, size_t& offset, float& out) {
    if (offset + 4 > data.size()) return false;
    std::memcpy(&out, data.data() + offset, 4);
    offset += 4;
    return true;
}

} // anonymous namespace

// ─── Serialize ───────────────────────────────────────────────────────────────

std::vector<uint8_t> Serialize(const ShaderReflection& refl) {
    std::vector<uint8_t> buf;
    buf.reserve(64 + refl.bindings.size() * 32);

    WriteU32(buf, kMagic);
    WriteU32(buf, kVersion);
    WriteU32(buf, refl.pushConstantSize);
    WriteU32(buf, static_cast<uint32_t>(refl.pushConstantStages));
    WriteU32(buf, static_cast<uint32_t>(refl.bindings.size()));

    for (const auto& b : refl.bindings) {
        WriteU32(buf, b.set);
        WriteU32(buf, b.binding);
        WriteU32(buf, static_cast<uint32_t>(b.type));
        WriteU32(buf, static_cast<uint32_t>(b.stages));
        WriteU32(buf, b.arraySize);
        WriteU32(buf, b.blockSize);
        WriteU32(buf, static_cast<uint32_t>(b.members.size()));
        for (const auto& m : b.members) {
            WriteU32(buf, m.offset);
            WriteU32(buf, m.size);
            WriteU8 (buf, static_cast<uint8_t>(m.uiType));
            WriteFloat(buf, m.minValue);
            WriteFloat(buf, m.maxValue);
            for (float dv : m.defaultValue) WriteFloat(buf, dv);
            WriteU32(buf, static_cast<uint32_t>(m.name.size()));
            buf.insert(buf.end(), m.name.begin(), m.name.end());
            WriteU32(buf, static_cast<uint32_t>(m.displayName.size()));
            buf.insert(buf.end(), m.displayName.begin(), m.displayName.end());
        }
        WriteU32(buf, static_cast<uint32_t>(b.name.size()));
        buf.insert(buf.end(), b.name.begin(), b.name.end());
        WriteU32(buf, static_cast<uint32_t>(b.displayName.size()));
        buf.insert(buf.end(), b.displayName.begin(), b.displayName.end());
    }

    // v7: generic shader-level metadata map (replaces v5 shadingModel/vertShader).
    WriteU32(buf, static_cast<uint32_t>(refl.metadata.size()));
    for (const auto& [k, v] : refl.metadata) {
        WriteU32(buf, static_cast<uint32_t>(k.size()));
        buf.insert(buf.end(), k.begin(), k.end());
        WriteU32(buf, static_cast<uint32_t>(v.size()));
        buf.insert(buf.end(), v.begin(), v.end());
    }

    // v6: vertex stage inputs (non-empty only for vertex reflections)
    WriteU32(buf, static_cast<uint32_t>(refl.vertexInputs.size()));
    for (const auto& vi : refl.vertexInputs) {
        WriteU32(buf, vi.location);
        WriteU32(buf, static_cast<uint32_t>(vi.format));
    }

    return buf;
}

// ─── Deserialize ─────────────────────────────────────────────────────────────

bool Deserialize(std::span<const uint8_t> data, ShaderReflection& out) {
    size_t offset = 0;
    uint32_t magic = 0, version = 0;

    if (!ReadU32(data, offset, magic) || magic != kMagic) {
        SA_LOG_ERROR("ShaderReflectionIO: bad magic (got {:08X})", magic);
        return false;
    }
    if (!ReadU32(data, offset, version) ||
        (version != 3u && version != 4u && version != 5u && version != 6u && version != kVersion)) {
        SA_LOG_ERROR("ShaderReflectionIO: unsupported version {}", version);
        return false;
    }
    // Annotation fields (uiType, min/max/default, displayName) were introduced in v5.
    // v4 files (committed baseline) use the basic layout without them.
    const bool v4 = (version >= 5u);
    const bool v5 = (version >= 5u);
    const bool v6 = (version >= 6u);

    uint32_t pcSize = 0, pcStages = 0, bindingCount = 0;
    if (!ReadU32(data, offset, pcSize))     return false;
    if (!ReadU32(data, offset, pcStages))   return false;
    if (!ReadU32(data, offset, bindingCount)) return false;

    ShaderReflection result;
    result.pushConstantSize   = pcSize;
    result.pushConstantStages = static_cast<RHIShaderStage>(pcStages);
    result.bindings.reserve(bindingCount);

    for (uint32_t i = 0; i < bindingCount; ++i) {
        uint32_t set = 0, binding = 0, type = 0, stages = 0;
        uint32_t arraySize = 0, blockSize = 0, memberCount = 0, nameLen = 0;
        if (!ReadU32(data, offset, set))         return false;
        if (!ReadU32(data, offset, binding))     return false;
        if (!ReadU32(data, offset, type))        return false;
        if (!ReadU32(data, offset, stages))      return false;
        if (!ReadU32(data, offset, arraySize))   return false;
        if (!ReadU32(data, offset, blockSize))   return false;
        if (!ReadU32(data, offset, memberCount)) return false;

        ShaderBindingDesc bd;
        bd.set       = set;
        bd.binding   = binding;
        bd.type      = static_cast<RHIDescriptorType>(type);
        bd.stages    = static_cast<RHIShaderStage>(stages);
        bd.arraySize = arraySize;
        bd.blockSize = blockSize;
        bd.members.reserve(memberCount);

        for (uint32_t m = 0; m < memberCount; ++m) {
            uint32_t mOffset = 0, mSize = 0, mNameLen = 0;
            if (!ReadU32(data, offset, mOffset)) return false;
            if (!ReadU32(data, offset, mSize))   return false;
            ShaderMemberDesc md;
            md.offset = mOffset;
            md.size   = mSize;
            if (v4) {
                uint8_t uiRaw = 0;
                if (!ReadU8   (data, offset, uiRaw))        return false;
                if (!ReadFloat(data, offset, md.minValue))  return false;
                if (!ReadFloat(data, offset, md.maxValue))  return false;
                for (float& dv : md.defaultValue)
                    if (!ReadFloat(data, offset, dv)) return false;
                md.uiType = static_cast<ParamUIType>(uiRaw);
            }
            if (!ReadU32(data, offset, mNameLen)) return false;
            if (offset + mNameLen > data.size()) {
                SA_LOG_ERROR("ShaderReflectionIO: member name truncated");
                return false;
            }
            md.name.assign(reinterpret_cast<const char*>(data.data() + offset), mNameLen);
            offset += mNameLen;
            if (v4) {
                uint32_t dispLen = 0;
                if (!ReadU32(data, offset, dispLen)) return false;
                if (offset + dispLen > data.size()) {
                    SA_LOG_ERROR("ShaderReflectionIO: member displayName truncated");
                    return false;
                }
                md.displayName.assign(reinterpret_cast<const char*>(data.data() + offset), dispLen);
                offset += dispLen;
            }
            bd.members.push_back(std::move(md));
        }

        if (!ReadU32(data, offset, nameLen)) return false;
        if (offset + nameLen > data.size()) {
            SA_LOG_ERROR("ShaderReflectionIO: binding name truncated");
            return false;
        }
        bd.name.assign(reinterpret_cast<const char*>(data.data() + offset), nameLen);
        offset += nameLen;
        if (v4) {
            uint32_t dispLen = 0;
            if (!ReadU32(data, offset, dispLen)) return false;
            if (offset + dispLen > data.size()) {
                SA_LOG_ERROR("ShaderReflectionIO: binding displayName truncated");
                return false;
            }
            bd.displayName.assign(reinterpret_cast<const char*>(data.data() + offset), dispLen);
            offset += dispLen;
        }

        result.bindings.push_back(std::move(bd));
    }

    // Shader-level metadata. v7+ stores a generic key→value map; v5/v6 stored two
    // fixed strings (shadingModel, vertShader) — migrate those into the map on read
    // so stale .refl files still load.
    const bool v7 = (version >= 7u);
    if (v7) {
        uint32_t metaCount = 0;
        if (!ReadU32(data, offset, metaCount)) return false;
        result.metadata.reserve(metaCount);
        for (uint32_t i = 0; i < metaCount; ++i) {
            uint32_t kLen = 0, vLen = 0;
            if (!ReadU32(data, offset, kLen) || offset + kLen > data.size()) return false;
            std::string key(reinterpret_cast<const char*>(data.data() + offset), kLen);
            offset += kLen;
            if (!ReadU32(data, offset, vLen) || offset + vLen > data.size()) return false;
            std::string val(reinterpret_cast<const char*>(data.data() + offset), vLen);
            offset += vLen;
            result.metadata.emplace_back(std::move(key), std::move(val));
        }
    } else if (v5) {
        uint32_t smLen = 0, vsLen = 0;
        if (!ReadU32(data, offset, smLen) || offset + smLen > data.size()) return false;
        if (smLen > 0)
            result.SetMeta("shadingModel",
                std::string(reinterpret_cast<const char*>(data.data() + offset), smLen));
        offset += smLen;

        if (!ReadU32(data, offset, vsLen) || offset + vsLen > data.size()) return false;
        if (vsLen > 0)
            result.SetMeta("vertShader",
                std::string(reinterpret_cast<const char*>(data.data() + offset), vsLen));
        offset += vsLen;
    }

    // v6: vertex stage inputs (v3-v5 files leave vertexInputs empty → backend
    // falls back to the legacy 4-attrib hardcoded layout for compatibility).
    if (v6) {
        uint32_t viCount = 0;
        if (!ReadU32(data, offset, viCount)) return false;
        result.vertexInputs.reserve(viCount);
        for (uint32_t i = 0; i < viCount; ++i) {
            uint32_t loc = 0, fmt = 0;
            if (!ReadU32(data, offset, loc)) return false;
            if (!ReadU32(data, offset, fmt)) return false;
            result.vertexInputs.push_back({loc, static_cast<RHIVertexFormat>(fmt)});
        }
    }

    out = std::move(result);
    return true;
}

// ─── File I/O ────────────────────────────────────────────────────────────────

bool LoadFromFile(const std::filesystem::path& path, ShaderReflection& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        SA_LOG_ERROR("ShaderReflectionIO: cannot open '{}'", path.string());
        return false;
    }
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return Deserialize(data, out);
}

bool SaveToFile(const std::filesystem::path& path, const ShaderReflection& refl) {
    auto data = Serialize(refl);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        SA_LOG_ERROR("ShaderReflectionIO: cannot write '{}'", path.string());
        return false;
    }
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return f.good();
}

} // namespace StellarAlia::RHI::ShaderReflectionIO
