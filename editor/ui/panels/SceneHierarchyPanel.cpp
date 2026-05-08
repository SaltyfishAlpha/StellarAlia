#include "ui/panels/SceneHierarchyPanel.hpp"

#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/EntityFactory.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "function/input/InputSystem.hpp"
#include "resource/AssetRegistry.hpp"
#include "resource/EntityTemplateRegistry.hpp"
#include "core/asset/AssetID.hpp"
#include "core/logs/Log.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

void SceneHierarchyPanel::SetRegistry(const Resource::AssetRegistry* registry) {
    m_registry = registry;
}
void SceneHierarchyPanel::SetTemplateRegistry(const EntityTemplateRegistry* tmplRegistry) {
    m_tmplRegistry = tmplRegistry;
}
void SceneHierarchyPanel::SetSceneLoadCallback(SceneLoadCallback cb) {
    m_onSceneLoad = std::move(cb);
}
void SceneHierarchyPanel::SetFocusEntityCallback(FocusEntityCallback cb) {
    m_onFocusEntity = std::move(cb);
}

// ── Hierarchy helpers ──────────────────────────────────────────────────────────

// Returns true if 'candidate' is equal to 'subtreeRoot' or is a descendant of it.
static bool IsInSubtree(entt::entity candidate, entt::entity subtreeRoot,
                        const entt::registry& reg) {
    if (candidate == subtreeRoot) return true;
    const auto* hc = reg.try_get<HierarchyComponent>(subtreeRoot);
    if (!hc) return false;
    for (entt::entity child : hc->children)
        if (IsInSubtree(candidate, child, reg)) return true;
    return false;
}

// Move 'child' to immediately before 'beforeSibling' in their shared parent's list.
static void MoveChildBefore(entt::entity child, entt::entity beforeSibling,
                             entt::registry& reg) {
    entt::entity parent = entt::null;
    if (const auto* hc = reg.try_get<HierarchyComponent>(child))
        parent = hc->parent;
    if (parent == entt::null) return;
    auto* phc = reg.try_get<HierarchyComponent>(parent);
    if (!phc) return;
    auto& ch = phc->children;
    auto it = std::find(ch.begin(), ch.end(), child);
    if (it == ch.end()) return;
    ch.erase(it);
    auto tgt = std::find(ch.begin(), ch.end(), beforeSibling);
    ch.insert(tgt != ch.end() ? tgt : ch.end(), child);
}

// Move 'child' to immediately after 'afterSibling' in their shared parent's list.
static void MoveChildAfter(entt::entity child, entt::entity afterSibling,
                            entt::registry& reg) {
    entt::entity parent = entt::null;
    if (const auto* hc = reg.try_get<HierarchyComponent>(child))
        parent = hc->parent;
    if (parent == entt::null) return;
    auto* phc = reg.try_get<HierarchyComponent>(parent);
    if (!phc) return;
    auto& ch = phc->children;
    auto it = std::find(ch.begin(), ch.end(), child);
    if (it == ch.end()) return;
    ch.erase(it);
    auto tgt = std::find(ch.begin(), ch.end(), afterSibling);
    ch.insert(tgt != ch.end() ? std::next(tgt) : ch.end(), child);
}

// ── DuplicateEntity ────────────────────────────────────────────────────────────
entt::entity SceneHierarchyPanel::DuplicateEntity(entt::entity src) {
    auto& reg = m_scene->Registry();

    const auto& srcTag = reg.get<TagComponent>(src);
    entt::entity dst = m_scene->CreateEntity(srcTag.name + " (Copy)");

    if (auto* t  = reg.try_get<TransformComponent>(src))        reg.emplace_or_replace<TransformComponent>(dst, *t);
    if (auto* c  = reg.try_get<CameraComponent>(src))           reg.emplace_or_replace<CameraComponent>(dst, *c);
    if (auto* dl = reg.try_get<DirectionalLightComponent>(src)) reg.emplace_or_replace<DirectionalLightComponent>(dst, *dl);
    if (auto* pl = reg.try_get<PointLightComponent>(src))       reg.emplace_or_replace<PointLightComponent>(dst, *pl);
    if (auto* sl = reg.try_get<SpotLightComponent>(src))        reg.emplace_or_replace<SpotLightComponent>(dst, *sl);
    if (auto* al = reg.try_get<AreaLightComponent>(src))        reg.emplace_or_replace<AreaLightComponent>(dst, *al);
    if (auto* sm = reg.try_get<StaticMeshComponent>(src))       reg.emplace_or_replace<StaticMeshComponent>(dst, *sm);
    if (auto* mo = reg.try_get<MaterialOverrideComponent>(src)) reg.emplace_or_replace<MaterialOverrideComponent>(dst, *mo);
    if (auto* an = reg.try_get<AnimatorComponent>(src))         reg.emplace_or_replace<AnimatorComponent>(dst, *an);
    if (auto* rb = reg.try_get<RigidBodyComponent>(src)) {
        RigidBodyComponent rbCopy = *rb;
        rbCopy.bodyId = ~0u;
        reg.emplace_or_replace<RigidBodyComponent>(dst, rbCopy);
    }
    if (auto* col = reg.try_get<ColliderComponent>(src))        reg.emplace_or_replace<ColliderComponent>(dst, *col);
    if (reg.any_of<StaticGeometryTag>(src))                     reg.emplace_or_replace<StaticGeometryTag>(dst);

    // Recursively duplicate children and re-parent under dst
    const auto* hc = reg.try_get<HierarchyComponent>(src);
    if (hc) {
        std::vector<entt::entity> srcChildren = hc->children;
        for (entt::entity child : srcChildren) {
            entt::entity childDst = DuplicateEntity(child);
            m_scene->SetParent(childDst, dst);
        }
    }

    return dst;
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
        if (commit) {
            reg.get<TagComponent>(entity).name = m_renameBuffer;
            m_renamingEntity = ~0u;
        } else if (lost) {
            // Click-away → commit; Escape → discard
            if (!ImGui::IsKeyDown(ImGuiKey_Escape))
                reg.get<TagComponent>(entity).name = m_renameBuffer;
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
            } else if (shiftHeld && m_shiftAnchor != ~0u) {
                SelectRange(ebits);
                m_primarySelected = ebits;
            } else {
                m_selection = { ebits };
                m_primarySelected = ebits;
                m_shiftAnchor     = ebits;
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_selection       = { ebits };
            m_primarySelected = ebits;
            m_shiftAnchor     = ebits;
            m_dblClickEntity  = ebits;
            m_dblClick.OnDoubleClicked();
        }

        // ── Right-click context menu ───────────────────────────────────────────
        ImGui::PushID(static_cast<int>(ebits));
        if (ImGui::BeginPopupContextItem()) {
            // Right-clicking always makes the node the primary selection.
            if (!m_selection.count(ebits)) {
                m_selection       = { ebits };
                m_primarySelected = ebits;
                m_shiftAnchor     = ebits;
            }

            if (ImGui::MenuItem("Rename\tF2")) {
                m_renamingEntity  = ebits;
                m_primarySelected = ebits;
                m_renameFocusNext = true;
                std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", name);
            }
            if (ImGui::BeginMenu("Create Child")) {
                if (ImGui::MenuItem("Empty Entity")) {
                    m_pendingCreate = { CreateOp::Empty, {}, entity };
                }
                if (m_tmplRegistry) {
                    std::string lastCategory;
                    for (const auto& entry : m_tmplRegistry->Entries()) {
                        if (entry.category != lastCategory) {
                            if (!entry.category.empty())
                                ImGui::SeparatorText(entry.category.c_str());
                            lastCategory = entry.category;
                        }
                        if (ImGui::MenuItem(entry.label.c_str()))
                            m_pendingCreate = { CreateOp::Template, entry.path, entity };
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate\tCtrl+D"))
                m_pendingDuplicates.push_back(entity);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete\tDel"))
                m_pendingDeletes.push_back(entity);

            ImGui::EndPopup();
        }
        ImGui::PopID();

        // ── Drag source ────────────────────────────────────────────────────────
        if (!m_dblClick.IsTracking() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            uint32_t bits = ebits;
            ImGui::SetDragDropPayload("SAENTITY", &bits, sizeof(bits));
            ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
        }

        // ── Drop target with three zones ───────────────────────────────────────
        if (ImGui::BeginDragDropTarget()) {
            const float itemTop = ImGui::GetItemRectMin().y;
            const float itemBot = ImGui::GetItemRectMax().y;
            const float h       = itemBot - itemTop;
            const float mouseY  = ImGui::GetMousePos().y;

            DnDOp::Mode zone =
                (mouseY < itemTop + h * 0.3f) ? DnDOp::BeforeSibling :
                (mouseY > itemBot - h * 0.3f) ? DnDOp::AfterSibling  :
                                                 DnDOp::AsChild;

            auto*       dl      = ImGui::GetWindowDrawList();
            const ImU32 lineCol = IM_COL32(80, 160, 255, 220);
            const float x0      = ImGui::GetItemRectMin().x;
            const float x1      = ImGui::GetItemRectMax().x;
            if (zone == DnDOp::BeforeSibling)
                dl->AddLine({x0, itemTop}, {x1, itemTop}, lineCol, 2.f);
            else if (zone == DnDOp::AfterSibling)
                dl->AddLine({x0, itemBot}, {x1, itemBot}, lineCol, 2.f);

            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAENTITY")) {
                entt::entity dragged = static_cast<entt::entity>(
                    *static_cast<const uint32_t*>(p->Data));
                if (dragged != entity)
                    m_pendingDnD = { dragged, entity, zone, true };
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
                m_pendingAssetDrop = {
                    fs::path(static_cast<const char*>(p->Data)), entity, true
                };
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
    m_pendingCreate = { CreateOp::Empty, {}, entt::null };
}

void SceneHierarchyPanel::RequestSpawnTemplate(const fs::path& templatePath) {
    m_pendingCreate = { CreateOp::Template, templatePath, entt::null };
}

// ── OnDraw ─────────────────────────────────────────────────────────────────────
void SceneHierarchyPanel::OnDraw() {
    auto& reg = m_scene->Registry();

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

    // ── Create-entity popup (triggered by menu bar or background right-click) ──
    // Shared lambda so both triggers show identical items.
    auto drawCreateItems = [&]() {
        if (ImGui::MenuItem("Empty Entity"))
            m_pendingCreate = { CreateOp::Empty, {}, entt::null };
        if (m_tmplRegistry) {
            std::string lastCategory;
            for (const auto& entry : m_tmplRegistry->Entries()) {
                if (entry.category != lastCategory) {
                    if (!entry.category.empty())
                        ImGui::SeparatorText(entry.category.c_str());
                    lastCategory = entry.category;
                }
                if (ImGui::MenuItem(entry.label.c_str()))
                    m_pendingCreate = { CreateOp::Template, entry.path, entt::null };
            }
        }
    };

    // ── Entity tree ────────────────────────────────────────────────────────────
    m_drawOrderBuild.clear();
    auto view = reg.view<TagComponent>();
    for (auto entity : view) {
        const auto* hc = reg.try_get<HierarchyComponent>(entity);
        if (hc && hc->parent != entt::null) continue;
        DrawNode(entity, reg);
    }
    m_drawOrder = m_drawOrderBuild;  // make this frame's order available next frame

    // Click on empty space → deselect
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()
        && !ImGui::IsAnyItemHovered()) {
        m_selection.clear();
        m_primarySelected = ~0u;
        m_shiftAnchor     = ~0u;
    }

    // Empty-area InvisibleButton: handles both right-click (create) and
    // drag-drop (detach to root). Using BeginPopupContextWindow+NoOpenOverItems
    // was broken because the InvisibleButton itself blocked the flag on the
    // next frame, so both are now attached to the same item.
    {
        const float h = std::max(ImGui::GetContentRegionAvail().y, 4.f);
        ImGui::InvisibleButton("##hier_bg_zone", ImVec2(-1.f, h));

        if (ImGui::BeginPopupContextItem("##hier_bg_ctx")) {
            drawCreateItems();
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAENTITY")) {
                entt::entity dragged = static_cast<entt::entity>(
                    *static_cast<const uint32_t*>(p->Data));
                m_pendingDnD = { dragged, entt::null, DnDOp::AsChild, true };
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
                m_pendingAssetDrop = {
                    fs::path(static_cast<const char*>(p->Data)), entt::null, true
                };
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ── Keyboard shortcuts via InputSystem ────────────────────────────────────
    const bool wantInput = (ImGui::IsWindowFocused() || ImGui::IsWindowHovered())
                         && m_renamingEntity == ~0u;
    if (wantInput) {
        if (m_input->WasActivated("SelectAll")) {
            m_selection.clear();
            for (auto entity : reg.view<TagComponent>())
                m_selection.insert(static_cast<uint32_t>(entity));
            if (!m_selection.empty())
                m_primarySelected = *m_selection.begin();
        } else if (!m_selection.empty()) {
            if (m_input->WasActivated("EntityDelete")) {
                for (uint32_t bits : m_selection)
                    m_pendingDeletes.push_back(static_cast<entt::entity>(bits));
            }
            if (m_input->WasActivated("EntityDuplicate")) {
                for (uint32_t bits : m_selection)
                    m_pendingDuplicates.push_back(static_cast<entt::entity>(bits));
            }
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

    // ── Execute deferred operations ────────────────────────────────────────────

    if (m_pendingCreate.kind != CreateOp::None) {
        CreateOp op = std::move(m_pendingCreate);
        m_pendingCreate = {};

        entt::entity e = entt::null;
        if (op.kind == CreateOp::Empty) {
            e = m_scene->CreateEntity("Entity");
        } else {
            auto spawned = SceneSerializer::SpawnFromTemplate(*m_scene, op.templatePath);
            if (!spawned.empty()) {
                e = spawned.front();
                m_scene->MarkMaterialDirty();
            }
        }

        if (reg.valid(e)) {
            if (reg.valid(op.parent))
                m_scene->SetParent(e, op.parent);
            m_selection       = { static_cast<uint32_t>(e) };
            m_primarySelected = static_cast<uint32_t>(e);
            m_shiftAnchor     = m_primarySelected;
        }
    }
    if (!m_pendingDuplicates.empty()) {
        std::vector<entt::entity> srcs = std::move(m_pendingDuplicates);
        m_pendingDuplicates.clear();
        m_selection.clear();
        entt::entity lastDst = entt::null;
        for (entt::entity src : srcs) {
            if (reg.valid(src)) {
                lastDst = DuplicateEntity(src);
                m_selection.insert(static_cast<uint32_t>(lastDst));
            }
        }
        if (reg.valid(lastDst)) {
            m_primarySelected = static_cast<uint32_t>(lastDst);
            m_shiftAnchor     = m_primarySelected;
            m_scene->MarkMaterialDirty();
        }
    }
    if (!m_pendingDeletes.empty()) {
        std::vector<entt::entity> es = std::move(m_pendingDeletes);
        m_pendingDeletes.clear();
        for (entt::entity e : es) {
            if (reg.valid(e)) {
                m_selection.erase(static_cast<uint32_t>(e));
                if (m_primarySelected == static_cast<uint32_t>(e))
                    m_primarySelected = ~0u;
                m_scene->DestroyEntity(e);
            }
        }
        if (m_primarySelected == ~0u && !m_selection.empty())
            m_primarySelected = *m_selection.begin();
    }

    // ── Asset drop execution ───────────────────────────────────────────────────
    if (m_pendingAssetDrop.valid) {
        const fs::path   assetPath = std::move(m_pendingAssetDrop.assetPath);
        const entt::entity parent  = m_pendingAssetDrop.parent;
        m_pendingAssetDrop = {};

        std::string ext = assetPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return static_cast<char>(::tolower(c)); });

        if (ext == ".sascene") {
            if (m_onSceneLoad) m_onSceneLoad(assetPath);
        } else if (ext == ".glb" || ext == ".gltf") {
            if (!m_registry) {
                SA_LOG_WARN("SceneHierarchyPanel: no registry — cannot instantiate mesh");
            } else {
                const Resource::AssetEntry* entry = m_registry->FindBySourcePath(assetPath);
                if (!entry || !entry->id.IsValid()) {
                    SA_LOG_WARN("SceneHierarchyPanel: '{}' not in registry — import it first",
                                assetPath.filename().string());
                } else {
                    entt::entity e = EntityFactory::CreateStaticMesh(
                        *m_scene, assetPath.stem().string(), entry->id);
                    if (reg.valid(parent))
                        m_scene->SetParent(e, parent);
                    m_selection       = { static_cast<uint32_t>(e) };
                    m_primarySelected = static_cast<uint32_t>(e);
                    m_shiftAnchor     = m_primarySelected;
                    m_scene->MarkMaterialDirty();
                }
            }
        }
        // Textures, materials, animations, skeletons → no independent scene object.
    }

    // ── Drag-and-drop execution ────────────────────────────────────────────────
    if (m_pendingDnD.valid) {
        DnDOp op = m_pendingDnD;
        m_pendingDnD = {};

        if (!reg.valid(op.dragged)) {
            // nothing
        } else if (op.target == entt::null) {
            // Detach to scene root
            m_scene->SetParent(op.dragged, entt::null);
        } else if (reg.valid(op.target) && op.dragged != op.target) {
            if (op.mode == DnDOp::AsChild) {
                // Cycle guard: prevent making an entity a child of its own descendant
                if (!IsInSubtree(op.target, op.dragged, reg))
                    m_scene->SetParent(op.dragged, op.target);
            } else {
                // Insert as sibling of target (share target's parent)
                entt::entity targetParent = entt::null;
                if (const auto* hc = reg.try_get<HierarchyComponent>(op.target))
                    targetParent = hc->parent;
                // Cycle guard: targetParent must not be inside dragged's subtree
                const bool safe = (targetParent == entt::null)
                    || !IsInSubtree(targetParent, op.dragged, reg);
                if (safe) {
                    m_scene->SetParent(op.dragged, targetParent);
                    if (op.mode == DnDOp::BeforeSibling)
                        MoveChildBefore(op.dragged, op.target, reg);
                    else
                        MoveChildAfter(op.dragged, op.target, reg);
                }
            }
        }
    }
}

} // namespace StellarAlia::Editor
