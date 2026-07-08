#include "ui/panels/SceneHierarchyPanel.hpp"
#include "EditorSelection.hpp"
#include "command/CommandManager.hpp"
#include "command/commands/EntityCommands.hpp"
#include "ui/AssetDragPayload.hpp"

#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/input/InputSystem.hpp"
#include "resource/AssetRegistry.hpp"
#include "resource/EntityTemplateRegistry.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

// ── SyncSelectionToCtx ───────────────────────────────────────────────────────
void SceneHierarchyPanel::SyncSelectionToCtx() {
    if (!m_selectionCtx) return;
    if (m_primarySelected == ~0u || m_selection.empty()) {
        m_selectionCtx->Clear();
        return;
    }
    // Build entity list with primary entity first so SelectEntities sets it as primary.
    std::vector<entt::entity> ents;
    ents.reserve(m_selection.size());
    ents.push_back(static_cast<entt::entity>(m_primarySelected));
    for (uint32_t b : m_selection)
        if (b != m_primarySelected)
            ents.push_back(static_cast<entt::entity>(b));
    m_selectionCtx->SelectEntities(ents);
}

// ── SelectRange ───────────────────────────────────────────────────────────────
void SceneHierarchyPanel::SelectRange(uint32_t to) {
    // Walk m_drawOrder (previous frame's visible sequence).
    // Select every entity between m_shiftAnchor and 'to', inclusive.
    const auto& order = m_drawOrder;
    auto itA = std::find(order.begin(), order.end(), m_shiftAnchor);
    auto itB = std::find(order.begin(), order.end(), to);
    if (itA == order.end() || itB == order.end()) {
        // Anchor or target not in visible order — fall back to single select.
        m_selection = { to };
        return;
    }
    if (itA > itB) std::swap(itA, itB);
    m_selection.clear();
    for (auto it = itA; it != std::next(itB); ++it)
        m_selection.insert(*it);
}

// ── DrawNode ───────────────────────────────────────────────────────────────────
void SceneHierarchyPanel::DrawNode(entt::entity entity, entt::registry& reg) {
    const auto& tag       = reg.get<TagComponent>(entity);
    const char* name      = tag.name.empty() ? "(unnamed)" : tag.name.c_str();
    const uint32_t ebits  = static_cast<uint32_t>(entity);
    const bool renaming   = (m_renamingEntity == ebits);

    const auto* hc          = reg.try_get<HierarchyComponent>(entity);
    const bool  hasChildren = hc && !hc->children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren)                     flags |= ImGuiTreeNodeFlags_Leaf;
    if (m_selection.count(ebits))         flags |= ImGuiTreeNodeFlags_Selected;

    m_drawOrderBuild.push_back(ebits);

    // Render tree node; when renaming use an empty label so SameLine() lands at
    // the text start and we can overlay an InputText there.
    const bool open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uint64_t>(entity)),
        flags, "%s", renaming ? "" : name);

    if (renaming) {
        // ── Inline rename ──────────────────────────────────────────────────────
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (m_renameFocusNext) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocusNext = false;
        }
        const bool commit = ImGui::InputText("##ri", m_renameBuffer,
                                             sizeof(m_renameBuffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue
                                             | ImGuiInputTextFlags_AutoSelectAll);
        const bool lost = ImGui::IsItemDeactivated();
        auto commitRename = [&]() {
            std::string oldName = reg.get<TagComponent>(entity).name;
            std::string newName = m_renameBuffer;
            if (oldName != newName) {
                if (m_cmdMgr && m_scene) {
                    EditorContext tmpCtx{};
                    tmpCtx.registry = &reg;
                    tmpCtx.scene    = m_scene;
                    tmpCtx.cmdMgr   = m_cmdMgr;
                    m_cmdMgr->Execute(
                        std::make_unique<RenameEntityCommand>(entity, std::move(oldName), std::move(newName)),
                        tmpCtx);
                } else {
                    reg.get<TagComponent>(entity).name = std::move(newName);
                }
            }
            m_renamingEntity = ~0u;
        };
        if (commit) {
            commitRename();
        } else if (lost) {
            // Click-away → commit; Escape → discard
            if (!ImGui::IsKeyDown(ImGuiKey_Escape))
                commitRename();
            else
                m_renamingEntity = ~0u;
        }
    } else {
        // ── Normal interaction ─────────────────────────────────────────────────
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            const bool ctrlHeld  = ImGui::GetIO().KeyCtrl;
            const bool shiftHeld = ImGui::GetIO().KeyShift;
            if (ctrlHeld) {
                if (m_selection.count(ebits)) m_selection.erase(ebits);
                else                          m_selection.insert(ebits);
                m_primarySelected = ebits;
                m_shiftAnchor     = ebits;
                SyncSelectionToCtx();
            } else if (shiftHeld && m_shiftAnchor != ~0u) {
                SelectRange(ebits);
                m_primarySelected = ebits;
                SyncSelectionToCtx();
            } else {
                if (m_selection.count(ebits) && m_selection.size() > 1)
                    m_pendingDeselectOthers = ebits;
                else {
                    m_selection       = { ebits };
                    m_primarySelected = ebits;
                    m_shiftAnchor     = ebits;
                    SyncSelectionToCtx();
                }
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_selection       = { ebits };
            m_primarySelected = ebits;
            m_shiftAnchor     = ebits;
            m_dblClickEntity  = ebits;
            m_dblClick.OnDoubleClicked();
            SyncSelectionToCtx();
        }

        // ── Right-click context menu ───────────────────────────────────────────
        ImGui::PushID(static_cast<int>(ebits));
        if (ImGui::BeginPopupContextItem()) {
            // Right-clicking always makes the node the primary selection.
            if (!m_selection.count(ebits)) {
                m_selection       = { ebits };
                m_primarySelected = ebits;
                m_shiftAnchor     = ebits;
                SyncSelectionToCtx();
            }

            if (ImGui::MenuItem("Rename\tF2")) {
                m_renamingEntity  = ebits;
                m_primarySelected = ebits;
                m_renameFocusNext = true;
                std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", name);
            }
            if (ImGui::BeginMenu("Create Child")) {
                if (ImGui::MenuItem("Empty Entity"))
                    m_presenter.RequestCreate(SceneHierarchyPresenter::CreateOp::Empty, {}, entity);
                if (m_tmplRegistry) {
                    std::string lastCategory;
                    for (const auto& entry : m_tmplRegistry->Entries()) {
                        if (entry.category != lastCategory) {
                            if (!entry.category.empty())
                                ImGui::SeparatorText(entry.category.c_str());
                            lastCategory = entry.category;
                        }
                        if (ImGui::MenuItem(entry.label.c_str()))
                            m_presenter.RequestCreate(SceneHierarchyPresenter::CreateOp::Template,
                                                      entry.path, entity);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate\tCtrl+D"))
                m_presenter.RequestDuplicate({ entity });
            ImGui::Separator();
            if (ImGui::MenuItem("Delete\tDel"))
                m_presenter.RequestDelete({ entity });

            ImGui::EndPopup();
        }
        ImGui::PopID();

        // ── Drag source ────────────────────────────────────────────────────────
        if (!m_dblClick.IsTracking() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            m_pendingDeselectOthers = ~0u; // drag confirmed, preserve multi-selection
            uint32_t bits = ebits;
            ImGui::SetDragDropPayload("SAENTITY", &bits, sizeof(bits));
            if (m_selection.count(ebits) && m_selection.size() > 1)
                ImGui::Text("%zu entities", m_selection.size());
            else
                ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
        }

        // ── Drop target with three zones ───────────────────────────────────────
        if (ImGui::BeginDragDropTarget()) {
            const float itemTop = ImGui::GetItemRectMin().y;
            const float itemBot = ImGui::GetItemRectMax().y;
            const float h       = itemBot - itemTop;
            const float mouseY  = ImGui::GetMousePos().y;

            using DnDMode = SceneHierarchyPresenter::DnDMode;
            DnDMode zone =
                (mouseY < itemTop + h * 0.3f) ? DnDMode::BeforeSibling :
                (mouseY > itemBot - h * 0.3f) ? DnDMode::AfterSibling  :
                                                 DnDMode::AsChild;

            auto*       dl      = ImGui::GetWindowDrawList();
            const ImU32 lineCol = IM_COL32(80, 160, 255, 220);
            const float x0      = ImGui::GetItemRectMin().x;
            const float x1      = ImGui::GetItemRectMax().x;
            if (zone == DnDMode::BeforeSibling)
                dl->AddLine({x0, itemTop}, {x1, itemTop}, lineCol, 2.f);
            else if (zone == DnDMode::AfterSibling)
                dl->AddLine({x0, itemBot}, {x1, itemBot}, lineCol, 2.f);

            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAENTITY")) {
                const uint32_t bits = *static_cast<const uint32_t*>(p->Data);
                std::vector<entt::entity> list;
                if (m_selection.count(bits) && m_selection.size() > 1) {
                    for (uint32_t b : m_drawOrder)
                        if (m_selection.count(b))
                            list.push_back(static_cast<entt::entity>(b));
                } else {
                    list = { static_cast<entt::entity>(bits) };
                }
                m_presenter.RequestReparent(std::move(list), entity, zone);
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
                if (p->DataSize >= static_cast<int>(sizeof(AssetDragPayload))) {
                    const auto& pl = *static_cast<const AssetDragPayload*>(p->Data);
                    m_presenter.RequestAssetDrop(
                        { fs::path(pl.path), entity, {}, {1.f, 0.f, 0.f, 0.f}, true });
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (open) {
        if (hasChildren) {
            // Iterate a copy: SetParent/reorder during DnD deferred ops must not
            // invalidate the range we're currently walking here.
            for (entt::entity child : hc->children)
                DrawNode(child, reg);
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::RequestCreateEmpty() {
    m_presenter.RequestCreate(SceneHierarchyPresenter::CreateOp::Empty, {}, entt::null);
}

void SceneHierarchyPanel::RequestSpawnTemplate(const fs::path& templatePath) {
    m_presenter.RequestCreate(SceneHierarchyPresenter::CreateOp::Template, templatePath, entt::null);
}

void SceneHierarchyPanel::SetSelection(entt::entity e) {
    m_selection.clear();
    m_primarySelected = static_cast<uint32_t>(e);
    m_selection.insert(m_primarySelected);
    m_shiftAnchor = m_primarySelected;
    if (m_selectionCtx) m_selectionCtx->SelectEntity(e);
}

void SceneHierarchyPanel::ClearSelection() {
    m_selection.clear();
    m_primarySelected = ~0u;
    m_shiftAnchor     = ~0u;
    if (m_selectionCtx) m_selectionCtx->Clear();
}

void SceneHierarchyPanel::TriggerAssetDrop(const fs::path& assetPath, const glm::vec3& spawnPos,
                                           const glm::quat& spawnRot) {
    m_presenter.RequestAssetDrop({ assetPath, entt::null, spawnPos, spawnRot, true });
}

// ── OnDraw ─────────────────────────────────────────────────────────────────────
void SceneHierarchyPanel::OnDraw() {
    auto& reg = m_scene->Registry();

    // Sync local selection mirror from EditorSelection (picks up presenter mutations).
    if (m_selectionCtx) {
        if (m_selectionCtx->GetType() == EditorSelectionType::Entity) {
            m_selection = m_selectionCtx->GetEntitySet();
            const entt::entity prim = m_selectionCtx->GetPrimaryEntity();
            m_primarySelected = (prim != entt::null) ? static_cast<uint32_t>(prim) : ~0u;
        } else if (m_selection.empty()) {
            // Nothing changed by presenter; don't clobber user's selection mid-drag.
        }
    }

    // ── Double-click result (short = focus, long = rename) ─────────────────────
    {
        const auto dblResult = m_dblClick.Update(ImGui::GetIO().DeltaTime);
        if (dblResult != DoubleClickClassifier::Result::None) {
            auto entity = static_cast<entt::entity>(m_dblClickEntity);
            if (reg.valid(entity)) {
                if (dblResult == DoubleClickClassifier::Result::Short) {
                    if (m_onFocusEntity && reg.all_of<WorldTransformComponent>(entity))
                        m_onFocusEntity(glm::vec3(reg.get<WorldTransformComponent>(entity).matrix[3]));
                } else {
                    auto& t = reg.get<TagComponent>(entity);
                    m_renamingEntity  = m_dblClickEntity;
                    m_primarySelected = m_dblClickEntity;
                    m_renameFocusNext = true;
                    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", t.name.c_str());
                }
            }
        }
    }

    // Shared lambda — create menu items for both toolbar and background right-click.
    auto drawCreateItems = [&]() {
        if (ImGui::MenuItem("Empty Entity"))
            m_presenter.RequestCreate(SceneHierarchyPresenter::CreateOp::Empty, {}, entt::null);
        if (m_tmplRegistry) {
            std::string lastCategory;
            for (const auto& entry : m_tmplRegistry->Entries()) {
                if (entry.category != lastCategory) {
                    if (!entry.category.empty())
                        ImGui::SeparatorText(entry.category.c_str());
                    lastCategory = entry.category;
                }
                if (ImGui::MenuItem(entry.label.c_str()))
                    m_presenter.RequestCreate(SceneHierarchyPresenter::CreateOp::Template,
                                              entry.path, entt::null);
            }
        }
    };

    // Flush deferred single-select: click on multi-selected item without dragging.
    if (m_pendingDeselectOthers != ~0u && !ImGui::IsMouseDown(0)) {
        const uint32_t bits = m_pendingDeselectOthers;
        m_selection           = { bits };
        m_primarySelected     = bits;
        m_shiftAnchor         = bits;
        m_pendingDeselectOthers = ~0u;
        SyncSelectionToCtx();
    }

    // ── Entity tree ────────────────────────────────────────────────────────────
    m_drawOrderBuild.clear();
    for (entt::entity entity : m_scene->GetRootOrder()) {
        if (reg.valid(entity))
            DrawNode(entity, reg);
    }
    m_drawOrder = m_drawOrderBuild;

    // Click on empty space → deselect
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()
        && !ImGui::IsAnyItemHovered()) {
        m_selection.clear();
        m_primarySelected = ~0u;
        m_shiftAnchor     = ~0u;
        if (m_selectionCtx) m_selectionCtx->Clear();
    }

    // Empty-area InvisibleButton: handles right-click (create) and entity DnD (detach to root).
    {
        const float h = std::max(ImGui::GetContentRegionAvail().y, 4.f);
        ImGui::InvisibleButton("##hier_bg_zone", ImVec2(-1.f, h));

        if (ImGui::BeginPopupContextItem("##hier_bg_ctx")) {
            drawCreateItems();
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAENTITY")) {
                const uint32_t bits = *static_cast<const uint32_t*>(p->Data);
                std::vector<entt::entity> list;
                if (m_selection.count(bits) && m_selection.size() > 1) {
                    for (uint32_t b : m_drawOrder)
                        if (m_selection.count(b))
                            list.push_back(static_cast<entt::entity>(b));
                } else {
                    list = { static_cast<entt::entity>(bits) };
                }
                m_presenter.RequestReparent(std::move(list), entt::null,
                                            SceneHierarchyPresenter::DnDMode::AsChild);
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
                if (p->DataSize >= static_cast<int>(sizeof(AssetDragPayload))) {
                    const auto& pl = *static_cast<const AssetDragPayload*>(p->Data);
                    m_presenter.RequestAssetDrop(
                        { fs::path(pl.path), entt::null, {}, {1.f, 0.f, 0.f, 0.f}, true });
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ── Keyboard shortcuts ────────────────────────────────────────────────────
    const bool wantInput = (ImGui::IsWindowFocused() || ImGui::IsWindowHovered())
                         && m_renamingEntity == ~0u;
    if (wantInput) {
        if (m_input->WasActivated("SelectAll")) {
            m_selection.clear();
            for (auto entity : reg.view<TagComponent>())
                m_selection.insert(static_cast<uint32_t>(entity));
            if (!m_selection.empty()) {
                m_primarySelected = *m_selection.begin();
                SyncSelectionToCtx();
            }
        } else if (!m_selection.empty()) {
            // EntityDelete and EntityDuplicate are dispatched globally via
            // EditorActionRegistry::PollAndDispatch; no panel-local handling needed.
            if (m_input->WasActivated("EntityRename") && m_primarySelected != ~0u) {
                auto sel = static_cast<entt::entity>(m_primarySelected);
                if (reg.valid(sel)) {
                    m_renamingEntity  = m_primarySelected;
                    m_renameFocusNext = true;
                    auto& t = reg.get<TagComponent>(sel);
                    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", t.name.c_str());
                }
            }
        }
    }
}

} // namespace StellarAlia::Editor
