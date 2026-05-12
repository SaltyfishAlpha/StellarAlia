#pragma once

#include "function/script/ScriptFieldSchema.hpp"

#include <string>
#include <unordered_map>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ScriptSchemaCache — className → ScriptClassSchema.
//
// Populated lazily by ScriptSystem::GetSchemaFor via the ScriptBridge two-step
// blob fetch (capacity-probe then real read). Cleared on every successful
// Compile so stale layouts can't leak across recompiles.
// ─────────────────────────────────────────────────────────────────────────────
class ScriptSchemaCache {
public:
    // Returns nullptr when not yet populated; the owner (ScriptSystem) inserts
    // entries via Insert() after fetching the blob.
    const ScriptClassSchema* Find(const std::string& className) const;

    // Move-in a freshly decoded schema; returns the stored pointer.
    const ScriptClassSchema* Insert(ScriptClassSchema schema);

    void Clear();
    size_t Size() const { return m_cache.size(); }

private:
    std::unordered_map<std::string, ScriptClassSchema> m_cache;
};

} // namespace StellarAlia
