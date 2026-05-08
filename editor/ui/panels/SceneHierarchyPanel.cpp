#include "ui/panels/SceneHierarchyPanel.hpp"

#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/EntityFactory.hpp"
#include "function/input/InputSystem.hpp"
#include "resource/AssetRegistry.hpp"
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
void SceneHierarchyPanel::SetSceneLoadCallback(SceneLoadCallback cb) {
    m_onSceneLoad = std::move(cb);
}

// UUIDs from assets/models/*.sameta — file-level mesh IDs used by
// EntityFactory (single-node GLTF files are cooked with the file UUID).
static constexpr std::string_view kBuiltinCubeUUID  = "c0be0000-0000-4000-0000-000000000001";
static constexpr std::string_view kBuiltinPlaneUUID = "c0be0000-0000-4000-0000-000000000002";

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
    if (auto* pbr= reg.try_get<PBRSurfaceComponent>(src))       reg.emplace_or_replace<PBRSurfaceComponent>(dst, *pbr);
    if (auto* mp = reg.try_get<MaterialParamComponent>(src))    reg.emplace_or_replace<MaterialParamComponent>(dst, *mp);
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
    if (!hasChildren)          flags |= ImGuiTreeNodeFlags_Leaf;
    if (ebits == m_selected)   flags |= ImGuiTreeNodeFlags_Selected;

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
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_selected = ebits;

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_renamingEntity  = ebits;
            m_selected        = ebits;
            m_renameFocusNext = true;
            std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", name);
        }

        // ── Right-click context menu ───────────────────────────────────────────
        ImGui::PushID(static_cast<int>(ebits));
        if (ImGui::BeginPopupContextItem()) {
            m_selected = ebits;

            if (ImGui::MenuItem("Rename\tF2")) {
                m_renamingEntity  = ebits;
                m_selected        = ebits;
                m_renameFocusNext = true;
                std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", name);
            }
            if (ImGui::BeginMenu("Create Child")) {
                if (ImGui::MenuItem("Empty Entity")) {
                    m_pendingCreateChild = entity; m_createKind = CreateKind::Empty;
                }
                ImGui::SeparatorText("Builtin Models");
                if (ImGui::MenuItem("Cube")) {
                    m_pendingCreateChild = entity; m_createKind = CreateKind::Cube;
                }
                if (ImGui::MenuItem("Plane")) {
                    m_pendingCreateChild = entity; m_createKind = CreateKind::Plane;
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate\tCtrl+D"))
                m_pendingDuplicate = entity;
            ImGui::Separator();
            if (ImGui::MenuItem("Delete\tDel"))
                m_pendingDelete = entity;

            ImGui::EndPopup();
        }
        ImGui::PopID();

        // ── Drag source ────────────────────────────────────────────────────────
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
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

// ── OnDraw ─────────────────────────────────────────────────────────────────────
void SceneHierarchyPanel::OnDraw() {
    auto& reg = m_scene->Registry();

    // ── Toolbar ────────────────────────────────────────────────────────────────
    if (ImGui::Button("+")) ImGui::OpenPopup("create_root_popup");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create Entity");
    if (ImGui::BeginPopup("create_root_popup")) {
        if (ImGui::MenuItem("Empty Entity")) {
            m_pendingCreateRoot = true; m_createKind = CreateKind::Empty;
        }
        ImGui::SeparatorText("Builtin Models");
        if (ImGui::MenuItem("Cube")) {
            m_pendingCreateRoot = true; m_createKind = CreateKind::Cube;
        }
        if (ImGui::MenuItem("Plane")) {
            m_pendingCreateRoot = true; m_createKind = CreateKind::Plane;
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    // ── Entity tree ────────────────────────────────────────────────────────────
    auto view = reg.view<TagComponent>();
    for (auto entity : view) {
        const auto* hc = reg.try_get<HierarchyComponent>(entity);
        if (hc && hc->parent != entt::null) continue;
        DrawNode(entity, reg);
    }

    // Click on empty space → deselect
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()
        && !ImGui::IsAnyItemHovered())
        m_selected = ~0u;

    // Drop onto empty window area → detach to scene root
    const float remainH = ImGui::GetContentRegionAvail().y;
    if (remainH > 2.f) {
        ImGui::InvisibleButton("##root_drop_zone", ImVec2(-1.f, remainH));
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
                         && m_renamingEntity == ~0u
                         && m_selected != ~0u;
    if (wantInput) {
        auto sel = static_cast<entt::entity>(m_selected);
        if (reg.valid(sel)) {
            if (m_input->WasActivated("EntityDelete"))
                m_pendingDelete = sel;

            const bool ctrlHeld = m_input->GetDeviceButton("Keyboard/LeftControl")  > 0.f
                                || m_input->GetDeviceButton("Keyboard/RightControl") > 0.f;
            if (ctrlHeld && m_input->WasActivated("EntityDuplicate"))
                m_pendingDuplicate = sel;

            if (m_input->WasActivated("EntityRename")) {
                m_renamingEntity  = m_selected;
                m_renameFocusNext = true;
                auto& t = reg.get<TagComponent>(sel);
                std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", t.name.c_str());
            }
        }
    }

    // ── Execute deferred operations ────────────────────────────────────────────

    // Helper: spawn an entity by kind at origin
    auto spawnByKind = [&](CreateKind kind, const char* name) -> entt::entity {
        switch (kind) {
        case CreateKind::Cube:
            return EntityFactory::CreateStaticMesh(
                *m_scene, "Cube", AssetID::FromString(kBuiltinCubeUUID));
        case CreateKind::Plane:
            return EntityFactory::CreateStaticMesh(
                *m_scene, "Plane", AssetID::FromString(kBuiltinPlaneUUID));
        default:
            return m_scene->CreateEntity(name);
        }
    };

    if (m_pendingCreateRoot) {
        m_pendingCreateRoot = false;
        entt::entity e = spawnByKind(m_createKind, "Entity");
        m_selected = static_cast<uint32_t>(e);
        if (m_createKind != CreateKind::Empty) m_scene->MarkMaterialDirty();
    }
    if (m_pendingCreateChild != entt::null) {
        entt::entity parent = m_pendingCreateChild;
        m_pendingCreateChild = entt::null;
        entt::entity child = spawnByKind(m_createKind, "Entity");
        m_scene->SetParent(child, parent);
        m_selected = static_cast<uint32_t>(child);
        if (m_createKind != CreateKind::Empty) m_scene->MarkMaterialDirty();
    }
    if (m_pendingDuplicate != entt::null) {
        entt::entity src = m_pendingDuplicate;
        m_pendingDuplicate = entt::null;
        if (reg.valid(src)) {
            entt::entity dst = DuplicateEntity(src);
            m_selected = static_cast<uint32_t>(dst);
            m_scene->MarkMaterialDirty();
        }
    }
    if (m_pendingDelete != entt::null) {
        entt::entity e = m_pendingDelete;
        m_pendingDelete = entt::null;
        if (reg.valid(e)) {
            if (static_cast<entt::entity>(m_selected) == e)
                m_selected = ~0u;
            m_scene->DestroyEntity(e);
        }
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
                    m_selected = static_cast<uint32_t>(e);
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
