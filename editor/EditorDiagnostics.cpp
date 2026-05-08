#include "EditorDiagnostics.hpp"
#include <algorithm>

namespace StellarAlia::Editor {

void EditorDiagnostics::Push(Diagnostic d) {
    m_items.push_back(std::move(d));
}

void EditorDiagnostics::Clear() {
    m_items.clear();
}

void EditorDiagnostics::ClearSource(DiagSource source) {
    m_items.erase(
        std::remove_if(m_items.begin(), m_items.end(),
            [source](const Diagnostic& d) { return d.source == source; }),
        m_items.end());
}

bool EditorDiagnostics::HasErrors() const {
    return std::any_of(m_items.begin(), m_items.end(),
        [](const Diagnostic& d) { return d.level == DiagLevel::Error; });
}

bool EditorDiagnostics::HasWarnings() const {
    return std::any_of(m_items.begin(), m_items.end(),
        [](const Diagnostic& d) { return d.level == DiagLevel::Warning; });
}

int EditorDiagnostics::ErrorCount() const {
    return static_cast<int>(std::count_if(m_items.begin(), m_items.end(),
        [](const Diagnostic& d) { return d.level == DiagLevel::Error; }));
}

int EditorDiagnostics::WarningCount() const {
    return static_cast<int>(std::count_if(m_items.begin(), m_items.end(),
        [](const Diagnostic& d) { return d.level == DiagLevel::Warning; }));
}

} // namespace StellarAlia::Editor
