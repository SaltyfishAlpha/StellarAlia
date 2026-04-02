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

bool ReadU32(std::span<const uint8_t> data, size_t& offset, uint32_t& out) {
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
            WriteU32(buf, static_cast<uint32_t>(m.name.size()));
            buf.insert(buf.end(), m.name.begin(), m.name.end());
        }
        WriteU32(buf, static_cast<uint32_t>(b.name.size()));
        buf.insert(buf.end(), b.name.begin(), b.name.end());
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
    if (!ReadU32(data, offset, version) || version != kVersion) {
        SA_LOG_ERROR("ShaderReflectionIO: unsupported version {}", version);
        return false;
    }

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
            if (!ReadU32(data, offset, mOffset))  return false;
            if (!ReadU32(data, offset, mSize))    return false;
            if (!ReadU32(data, offset, mNameLen)) return false;
            if (offset + mNameLen > data.size()) {
                SA_LOG_ERROR("ShaderReflectionIO: member name truncated");
                return false;
            }
            ShaderMemberDesc md;
            md.offset = mOffset;
            md.size   = mSize;
            md.name.assign(reinterpret_cast<const char*>(data.data() + offset), mNameLen);
            offset += mNameLen;
            bd.members.push_back(std::move(md));
        }

        if (!ReadU32(data, offset, nameLen)) return false;
        if (offset + nameLen > data.size()) {
            SA_LOG_ERROR("ShaderReflectionIO: binding name truncated");
            return false;
        }
        bd.name.assign(reinterpret_cast<const char*>(data.data() + offset), nameLen);
        offset += nameLen;

        result.bindings.push_back(std::move(bd));
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
