#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"

namespace StellarAlia::Editor {

class WorldSettingsPresenter final : public IPresenter {
public:
    explicit WorldSettingsPresenter(EditorContext& ctx);
    void Update(float dt) override;

    void RequestApplySettings(bool updateIBL = false);
    void RequestRebakeIBL();

private:
    EditorContext& m_ctx;
    bool m_pendingRebake   = false;
    bool m_pendingApply    = false;
    bool m_pendingApplyIBL = false;
};

} // namespace StellarAlia::Editor
