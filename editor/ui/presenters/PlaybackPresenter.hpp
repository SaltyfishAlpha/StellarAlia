#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"
#include "engine/EnginePlayState.hpp"

namespace StellarAlia::Editor {

class PlaybackPresenter final : public IPresenter {
public:
    explicit PlaybackPresenter(EditorContext& ctx);
    void Update(float dt) override;
    void RequestSetPlayState(EnginePlayState state);

private:
    EditorContext&  m_ctx;
    bool            m_hasPending  = false;
    EnginePlayState m_pendingState;
};

} // namespace StellarAlia::Editor
