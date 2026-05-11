#include "ui/presenters/PlaybackPresenter.hpp"

#include "engine/Application.hpp"

namespace StellarAlia::Editor {

PlaybackPresenter::PlaybackPresenter(EditorContext& ctx)
    : m_ctx(ctx)
    , m_pendingState(EnginePlayState::Editing)
{}

void PlaybackPresenter::Update(float /*dt*/) {
    if (!m_hasPending) return;
    m_hasPending = false;
    if (m_ctx.app)
        m_ctx.app->SetPlayState(m_pendingState);
}

void PlaybackPresenter::RequestSetPlayState(EnginePlayState state) {
    m_hasPending   = true;
    m_pendingState = state;
}

} // namespace StellarAlia::Editor
