#include "function/script/ScriptSchemaCache.hpp"

namespace StellarAlia {

const ScriptClassSchema* ScriptSchemaCache::Find(const std::string& className) const {
    auto it = m_cache.find(className);
    return it == m_cache.end() ? nullptr : &it->second;
}

const ScriptClassSchema* ScriptSchemaCache::Insert(ScriptClassSchema schema) {
    std::string key = schema.className;
    auto [it, ok] = m_cache.emplace(std::move(key), std::move(schema));
    return &it->second;
}

void ScriptSchemaCache::Clear() {
    m_cache.clear();
}

} // namespace StellarAlia
