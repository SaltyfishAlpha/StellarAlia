#include "ui/presenters/ConsolePanelPresenter.hpp"

namespace StellarAlia::Editor {

ConsolePanelPresenter::ConsolePanelPresenter(EditorContext& ctx)
    : m_logSink(ctx.logCapture ? ctx.logCapture->GetSink() : nullptr) {}

void ConsolePanelPresenter::Update(float dt) {
    m_gotNew = false;
    if (!m_logSink) return;

    m_drainTimer += dt;
    if (m_drainTimer < kDrainInterval) return;
    m_drainTimer = 0.f;

    auto newEntries = m_logSink->Drain();
    for (auto& e : newEntries) {
        const int lvl = static_cast<int>(e.level);
        if (lvl < 0 || lvl > 5 || !m_logLevelShow[lvl]) continue;

        ++m_logUnread;
        m_gotNew = true;

        if (m_logEntries.size() >= kMaxEntries)
            m_logEntries.erase(m_logEntries.begin());
        if (e.loggerName == "script") {
            if (m_scriptEntries.size() >= kMaxEntries)
                m_scriptEntries.erase(m_scriptEntries.begin());
            m_scriptEntries.push_back(e);
        }
        m_logEntries.push_back(std::move(e));
    }
}

} // namespace StellarAlia::Editor
