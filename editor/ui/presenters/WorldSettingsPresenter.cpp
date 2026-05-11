#include "ui/presenters/WorldSettingsPresenter.hpp"

#include "engine/Application.hpp"

namespace StellarAlia::Editor {

WorldSettingsPresenter::WorldSettingsPresenter(EditorContext& ctx)
    : m_ctx(ctx)
{}

void WorldSettingsPresenter::Update(float /*dt*/) {
    if (!m_ctx.app || !m_ctx.scene) return;

    if (m_pendingRebake) {
        m_pendingRebake   = false;
        m_pendingApply    = false;
        m_pendingApplyIBL = false;
        m_ctx.app->GetRenderer().RebakeIBL(m_ctx.scene->GetWorldSettings());
        return;
    }
    if (m_pendingApply) {
        const bool ibl    = m_pendingApplyIBL;
        m_pendingApply    = false;
        m_pendingApplyIBL = false;
        m_ctx.app->GetRenderer().ApplyWorldSettings(m_ctx.scene->GetWorldSettings(), ibl);
    }
}

void WorldSettingsPresenter::RequestApplySettings(bool updateIBL) {
    m_pendingApply    = true;
    m_pendingApplyIBL = m_pendingApplyIBL || updateIBL;
}

void WorldSettingsPresenter::RequestRebakeIBL() {
    m_pendingRebake = true;
}

} // namespace StellarAlia::Editor
