#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"

#include <filesystem>

namespace StellarAlia::Editor {

class ShortcutsPresenter final : public IPresenter {
public:
    explicit ShortcutsPresenter(EditorContext& ctx);
    void Update(float dt) override;

    void RequestDefault();
    void RequestReload();
    void RequestApply();
    void RequestSave();
    void RequestNFDImport();
    void RequestNFDExport();

private:
    void RunRegisterMaps();

    EditorContext&        m_ctx;
    std::filesystem::path m_defaultConfigPath;

    bool m_pendingDefault   = false;
    bool m_pendingReload    = false;
    bool m_pendingApply     = false;
    bool m_pendingSave      = false;
    bool m_pendingNFDImport = false;
    bool m_pendingNFDExport = false;
};

} // namespace StellarAlia::Editor
