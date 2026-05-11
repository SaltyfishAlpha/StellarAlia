#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"

namespace StellarAlia::Editor {

class PostProcessPresenter final : public IPresenter {
public:
    explicit PostProcessPresenter(EditorContext& ctx);
    void Update(float dt) override;

    void RequestApply();

private:
    EditorContext& m_ctx;
    bool m_pendingApply = false;
};

} // namespace StellarAlia::Editor
