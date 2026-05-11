#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"
#include "project/ProjectManager.hpp"

#include <filesystem>
#include <string>

namespace StellarAlia::Editor {

class ProjectBrowserPresenter final : public IPresenter {
public:
    explicit ProjectBrowserPresenter(EditorContext& ctx);
    void Update(float dt) override;

    void RequestCreateProject(std::filesystem::path dir,
                              std::string           name,
                              ProjectTemplate       tmpl);

    // Result channels — consume once per frame from within BeginPopupModal block.
    bool ConsumeCreateSuccess(std::filesystem::path& outPath);
    bool ConsumeCreateError(std::string& outMsg);

private:
    EditorContext&        m_ctx;
    std::filesystem::path m_engineAssetsDir;

    bool                  m_hasPendingCreate = false;
    std::filesystem::path m_createDir;
    std::string           m_createName;
    ProjectTemplate       m_createTmpl = ProjectTemplate::Default;

    std::filesystem::path m_createSuccess;
    bool                  m_hasCreateSuccess = false;
    std::string           m_createError;
    bool                  m_hasCreateError   = false;
};

} // namespace StellarAlia::Editor
