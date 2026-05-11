#include "ui/presenters/PostProcessPresenter.hpp"

#include "engine/Application.hpp"

namespace StellarAlia::Editor {

PostProcessPresenter::PostProcessPresenter(EditorContext& ctx)
    : m_ctx(ctx)
{}

void PostProcessPresenter::Update(float /*dt*/) {
    if (!m_pendingApply) return;
    m_pendingApply = false;
    if (m_ctx.app && m_ctx.scene)
        m_ctx.app->GetRenderer().ApplyWorldSettings(
            m_ctx.scene->GetWorldSettings(), /*updateIBL=*/false);
}

void PostProcessPresenter::RequestApply() {
    m_pendingApply = true;
}

} // namespace StellarAlia::Editor
