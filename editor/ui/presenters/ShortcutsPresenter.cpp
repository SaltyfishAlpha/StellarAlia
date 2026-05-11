#include "ui/presenters/ShortcutsPresenter.hpp"

#include "ApplicationPath.hpp"
#include "config/EditorShortcutConfig.hpp"
#include "input/EditorInputMaps.hpp"
#include "function/input/InputSystem.hpp"
#include "core/logs/Log.hpp"

#if __has_include(<nfd.h>)
#include <nfd.h>
#define SA_SHORTCUTS_NFD 1
#endif

namespace StellarAlia::Editor {

ShortcutsPresenter::ShortcutsPresenter(EditorContext& ctx)
    : m_ctx(ctx)
    , m_defaultConfigPath(std::filesystem::path(StellarAliaApp::BIN_DIR) / "editor_shortcuts.json")
{}

void ShortcutsPresenter::RunRegisterMaps() {
    if (m_ctx.shortcuts && m_ctx.input)
        m_ctx.input->RegisterMaps(m_ctx.shortcuts->ApplyTo(MakeViewportMaps()));
}

void ShortcutsPresenter::Update(float /*dt*/) {
    if (m_pendingNFDImport) {
        m_pendingNFDImport = false;
#ifdef SA_SHORTCUTS_NFD
        if (NFD_Init() == NFD_OKAY) {
            nfdchar_t* outPath = nullptr;
            const nfdfilteritem_t filter[] = { { "Shortcut config", "json" } };
            if (NFD_OpenDialogU8(&outPath, filter, 1, nullptr) == NFD_OKAY && outPath) {
                if (m_ctx.shortcuts)
                    m_ctx.shortcuts->ImportFrom(std::filesystem::path(outPath));
                NFD_FreePathU8(outPath);
                RunRegisterMaps();
            }
            NFD_Quit();
        }
#endif
        return;
    }
    if (m_pendingNFDExport) {
        m_pendingNFDExport = false;
#ifdef SA_SHORTCUTS_NFD
        if (NFD_Init() == NFD_OKAY) {
            nfdchar_t* outPath = nullptr;
            const nfdfilteritem_t filter[] = { { "Shortcut config", "json" } };
            if (NFD_SaveDialogU8(&outPath, filter, 1, nullptr, "shortcuts.json") == NFD_OKAY && outPath) {
                if (m_ctx.shortcuts)
                    m_ctx.shortcuts->ExportTo(std::filesystem::path(outPath));
                NFD_FreePathU8(outPath);
            }
            NFD_Quit();
        }
#endif
        return;
    }
    if (m_pendingDefault) {
        m_pendingDefault = false;
        if (m_ctx.shortcuts)
            m_ctx.shortcuts->Load(m_defaultConfigPath);
        RunRegisterMaps();
        return;
    }
    if (m_pendingReload) {
        m_pendingReload = false;
        if (m_ctx.shortcuts)
            m_ctx.shortcuts->Reload();
        RunRegisterMaps();
        return;
    }
    if (m_pendingApply) {
        m_pendingApply = false;
        RunRegisterMaps();
        if (m_ctx.shortcuts)
            m_ctx.shortcuts->ClearDirty();
        return;
    }
    if (m_pendingSave) {
        m_pendingSave = false;
        RunRegisterMaps();
        if (m_ctx.shortcuts) {
            m_ctx.shortcuts->Save();
            m_ctx.shortcuts->ClearDirty();
        }
    }
}

void ShortcutsPresenter::RequestDefault()   { m_pendingDefault   = true; }
void ShortcutsPresenter::RequestReload()    { m_pendingReload    = true; }
void ShortcutsPresenter::RequestApply()     { m_pendingApply     = true; }
void ShortcutsPresenter::RequestSave()      { m_pendingSave      = true; }
void ShortcutsPresenter::RequestNFDImport() { m_pendingNFDImport = true; }
void ShortcutsPresenter::RequestNFDExport() { m_pendingNFDExport = true; }

} // namespace StellarAlia::Editor
