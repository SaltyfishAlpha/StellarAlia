#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorLogCapture.hpp"
#include "EditorContext.hpp"
#include <vector>
#include <memory>

namespace StellarAlia::Editor {

class ConsolePanelPresenter final : public IPresenter {
public:
    explicit ConsolePanelPresenter(EditorContext& ctx);
    void Update(float dt) override;

    const std::vector<LogEntry>& GetEngineEntries() const { return m_logEntries; }
    const std::vector<LogEntry>& GetScriptEntries() const { return m_scriptEntries; }
    bool  HasNew()      const { return m_gotNew; }
    int   UnreadCount() const { return m_logUnread; }
    void  ResetUnread()       { m_logUnread = 0; }
    void  ClearEngine()       { m_logEntries.clear(); }
    void  ClearScript()       { m_scriptEntries.clear(); }
    bool  GetLevelShow(int i) const { return m_logLevelShow[i]; }
    void  ToggleLevel(int i)        { m_logLevelShow[i] = !m_logLevelShow[i]; }

private:
    std::shared_ptr<EditorLogSink> m_logSink;
    std::vector<LogEntry>          m_logEntries;    // all engine logs
    std::vector<LogEntry>          m_scriptEntries; // "script" logger subset
    int   m_logUnread   = 0;
    bool  m_gotNew      = false;
    float m_drainTimer  = 0.f;
    bool  m_logLevelShow[6] = { false, false, true, true, true, true };

    static constexpr size_t kMaxEntries    = 2000;
    static constexpr float  kDrainInterval = 1.f;
};

} // namespace StellarAlia::Editor
