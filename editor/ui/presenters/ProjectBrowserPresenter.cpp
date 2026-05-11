#include "ui/presenters/ProjectBrowserPresenter.hpp"

#include "engine/Application.hpp"
#include "core/logs/Log.hpp"

namespace StellarAlia::Editor {

ProjectBrowserPresenter::ProjectBrowserPresenter(EditorContext& ctx)
    : m_ctx(ctx)
    , m_engineAssetsDir(ctx.app ? ctx.app->GetDesc().engineAssetsDir : std::filesystem::path{})
{}

void ProjectBrowserPresenter::Update(float /*dt*/) {
    if (!m_hasPendingCreate) return;
    m_hasPendingCreate = false;

    const std::filesystem::path saproject = ProjectManager::CreateProject(
        m_createDir, m_createName, m_createTmpl, m_engineAssetsDir);

    if (saproject.empty()) {
        m_createError      = "Failed to create project — check the log for details.";
        m_hasCreateError   = true;
        m_hasCreateSuccess = false;
    } else {
        m_createSuccess    = saproject;
        m_hasCreateSuccess = true;
        m_hasCreateError   = false;
    }
}

void ProjectBrowserPresenter::RequestCreateProject(std::filesystem::path dir,
                                                    std::string           name,
                                                    ProjectTemplate       tmpl) {
    m_createDir        = std::move(dir);
    m_createName       = std::move(name);
    m_createTmpl       = tmpl;
    m_hasPendingCreate = true;
    m_hasCreateSuccess = false;
    m_hasCreateError   = false;
}

bool ProjectBrowserPresenter::ConsumeCreateSuccess(std::filesystem::path& outPath) {
    if (!m_hasCreateSuccess) return false;
    m_hasCreateSuccess = false;
    outPath = std::move(m_createSuccess);
    return true;
}

bool ProjectBrowserPresenter::ConsumeCreateError(std::string& outMsg) {
    if (!m_hasCreateError) return false;
    m_hasCreateError = false;
    outMsg = std::move(m_createError);
    return true;
}

} // namespace StellarAlia::Editor
