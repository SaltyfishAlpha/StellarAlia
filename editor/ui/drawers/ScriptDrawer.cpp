#include "ui/drawers/ScriptDrawer.hpp"
#include "EditorContext.hpp"
#include "engine/Application.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/script/ScriptSystem.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "resource/AssetRegistry.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace StellarAlia::Editor {

namespace fs = std::filesystem;

namespace {

// Ensure sc.fields[name] holds a variant alternative of type T.
// On insert / kind mismatch, seed from schema.defaults (captured from the C#
// field initializer via `Activator.CreateInstance`); zero-init when no
// initializer or the default has the wrong variant alt.
template<typename T>
T* EnsureValue(ScriptComponent& sc, const ScriptClassSchema& schema,
               const std::string& name)
{
    auto& v = sc.fields[name];
    if (T* p = std::get_if<T>(&v)) return p;
    auto dit = schema.defaults.find(name);
    if (dit != schema.defaults.end()) {
        if (const T* d = std::get_if<T>(&dit->second)) {
            v = *d;
            return std::get_if<T>(&v);
        }
    }
    v = T{};
    return std::get_if<T>(&v);
}

void DrawScriptField(ScriptComponent& sc, entt::entity e,
                     const ScriptClassSchema& schema,
                     const ScriptFieldDescriptor& f, EditorContext& ctx,
                     bool& outChanged)
{
    // [HideInInspector] — field still serializes, just not shown.
    if (f.hidden) return;
    // [Header("...")] — emit separator before the field.
    if (!f.header.empty()) ImGui::SeparatorText(f.header.c_str());

    const std::string& label = f.label.empty() ? f.name : f.label;
    ImGui::PushID(f.name.c_str());

    bool changedThisField = false;
    switch (f.kind) {
        case ScriptFieldKind::Bool: {
            bool* p = EnsureValue<bool>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label](bool* x){ return ImGui::Checkbox(label.c_str(), x); });
            break;
        }
        case ScriptFieldKind::Int32: {
            int32_t* p = EnsureValue<int32_t>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label, &f](int32_t* x){
                    int tmp = *x;
                    const bool ch = f.hasRange
                        ? ImGui::SliderInt(label.c_str(), &tmp,
                                           static_cast<int>(f.rangeMin),
                                           static_cast<int>(f.rangeMax))
                        : ImGui::DragInt(label.c_str(), &tmp);
                    if (ch) { *x = tmp; return true; }
                    return false;
                });
            break;
        }
        case ScriptFieldKind::Float: {
            float* p = EnsureValue<float>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label, &f](float* x){
                    return f.hasRange
                        ? ImGui::SliderFloat(label.c_str(), x, f.rangeMin, f.rangeMax)
                        : ImGui::DragFloat(label.c_str(), x, 0.01f);
                });
            break;
        }
        case ScriptFieldKind::Vec2: {
            glm::vec2* p = EnsureValue<glm::vec2>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label](glm::vec2* x){ return ImGui::DragFloat2(label.c_str(), glm::value_ptr(*x), 0.01f); });
            break;
        }
        case ScriptFieldKind::Vec3: {
            glm::vec3* p = EnsureValue<glm::vec3>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label](glm::vec3* x){ return ImGui::DragFloat3(label.c_str(), glm::value_ptr(*x), 0.01f); });
            break;
        }
        case ScriptFieldKind::Vec4: {
            glm::vec4* p = EnsureValue<glm::vec4>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label](glm::vec4* x){ return ImGui::DragFloat4(label.c_str(), glm::value_ptr(*x), 0.01f); });
            break;
        }
        case ScriptFieldKind::String: {
            std::string* p = EnsureValue<std::string>(sc, schema, f.name);
            changedThisField = TrackedFieldEdit(p, ctx, "Edit " + f.name,
                [&label](std::string* x){
                    char buf[512];
                    std::strncpy(buf, x->c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) { *x = buf; return true; }
                    return false;
                });
            break;
        }
        case ScriptFieldKind::Color: {
            // The variant alt is determined by the C# field type (Color or
            // Color3-equivalent). Default to vec4 on first display.
            auto& v = sc.fields[f.name];
            if (!std::holds_alternative<glm::vec3>(v) && !std::holds_alternative<glm::vec4>(v))
                v = glm::vec4{1.f};
            if (auto* p4 = std::get_if<glm::vec4>(&v)) {
                changedThisField = TrackedFieldEdit(p4, ctx, "Edit " + f.name,
                    [&label](glm::vec4* x){ return ImGui::ColorEdit4(label.c_str(), glm::value_ptr(*x)); });
            } else if (auto* p3 = std::get_if<glm::vec3>(&v)) {
                changedThisField = TrackedFieldEdit(p3, ctx, "Edit " + f.name,
                    [&label](glm::vec3* x){ return ImGui::ColorEdit3(label.c_str(), glm::value_ptr(*x)); });
            }
            break;
        }
        case ScriptFieldKind::AssetRef: {
            AssetID* p = EnsureValue<AssetID>(sc, schema, f.name);
            AssetID before = *p;
            const char* filter = f.typeHint.empty() ? nullptr : f.typeHint.c_str();
            // DrawAssetIDField already accepts SAASSET drops on the picker button.
            if (DrawAssetIDField(label.c_str(), *p, filter, ctx.assetReg)) {
                if (ctx.cmdMgr && before != *p) {
                    ctx.cmdMgr->Execute(
                        std::make_unique<SetFieldCommand<AssetID>>(
                            p, before, *p, "Edit " + f.name),
                        ctx);
                }
                changedThisField = true;
            }
            break;
        }
        case ScriptFieldKind::EntityRef: {
            uint64_t* p = EnsureValue<uint64_t>(sc, schema, f.name);
            uint64_t before = *p;
            // Button label: target entity name + sceneLocalId, or "(none)".
            std::string btnLabel = "(none)";
            entt::entity target = (ctx.scene && *p != 0)
                ? ctx.scene->FindBySceneLocalId(*p)
                : entt::null;
            if (*p != 0) {
                if (target != entt::null && ctx.registry) {
                    const auto* tag = ctx.registry->try_get<TagComponent>(target);
                    btnLabel = (tag ? tag->name : std::string{"(unnamed)"});
                    btnLabel += "  #" + std::to_string(*p);
                } else {
                    btnLabel = "(missing #" + std::to_string(*p) + ")";
                }
            }
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine();
            const float clearW   = (*p != 0) ? 26.f : 0.f;
            const float btnWidth = std::max(10.f, ImGui::GetContentRegionAvail().x - clearW);
            ImGui::PushID("##entref");
            if (ImGui::Button(btnLabel.c_str(), ImVec2(btnWidth, 0)))
                ImGui::OpenPopup("##entity_pick");
            // SAENTITY drop target on the button.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SAENTITY")) {
                    if (pl->DataSize >= static_cast<int>(sizeof(uint32_t)) && ctx.registry) {
                        uint32_t bits = *static_cast<const uint32_t*>(pl->Data);
                        entt::entity e = entt::entity{bits};
                        if (ctx.registry->valid(e)) {
                            if (const auto* eid = ctx.registry->try_get<EntityIdComponent>(e))
                                *p = eid->sceneLocalId;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (*p != 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("\xc3\x97")) *p = 0;
            }
            if (ImGui::BeginPopup("##entity_pick")) {
                static char filter[128] = {};
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##flt", "Filter\xe2\x80\xa6", filter, sizeof(filter));
                ImGui::Separator();
                ImGui::BeginChild("##list", ImVec2(280, 200), false);
                if (ctx.registry) {
                    auto view = ctx.registry->view<EntityIdComponent, TagComponent>();
                    for (auto e : view) {
                        const auto& tag = view.get<TagComponent>(e);
                        if (filter[0] != '\0' &&
                            tag.name.find(filter) == std::string::npos) continue;
                        const auto& eid = view.get<EntityIdComponent>(e);
                        const bool selected = (eid.sceneLocalId == *p);
                        std::string lbl = tag.name + "  #" + std::to_string(eid.sceneLocalId);
                        if (ImGui::Selectable(lbl.c_str(), selected)) {
                            *p = eid.sceneLocalId;
                            filter[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            ImGui::PopID();
            if (before != *p) {
                if (ctx.cmdMgr) {
                    ctx.cmdMgr->Execute(
                        std::make_unique<SetFieldCommand<uint64_t>>(
                            p, before, *p, "Edit " + f.name),
                        ctx);
                }
                changedThisField = true;
            }
            break;
        }
        case ScriptFieldKind::Enum:
            ImGui::TextDisabled("%s  (later in #75)", label.c_str());
            break;
        case ScriptFieldKind::Unsupported:
        default:
            ImGui::TextDisabled("%s  (unsupported type)", label.c_str());
            break;
    }

    if (changedThisField) {
        outChanged = true;
        // Live-sync to the C# instance during Play so OnUpdate sees the new value
        // next frame. Single-field delta keeps the per-drag-tick blob size constant.
        if (ctx.app) {
            auto& ss = ctx.app->GetScriptSystem();
            if (ss.IsPlaying())
                ss.InjectSingleField(static_cast<uint64_t>(e), f.name, sc.fields[f.name], f.kind);
        }
    }

    // [Tooltip("...")] — show when the last-submitted item is hovered.
    if (!f.tooltip.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", f.tooltip.c_str());

    ImGui::PopID();
}

} // anonymous

bool ScriptDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                            Scene& scene, EditorContext& ctx) {
    (void)scene;
    auto* sc = reg.try_get<ScriptComponent>(entity);
    if (!sc) return false;

    bool open = ImGui::CollapsingHeader("Script",
                    HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
    if (RemoveComponentButton<ScriptComponent>("x##rem_script", reg, entity, ctx, "Remove Script")) return true;
    if (!open) return true;

    // ── Script asset (AssetID) ────────────────────────────────────────────────
    AssetID beforePick = sc->scriptId;
    if (DrawAssetIDField("Script", sc->scriptId, "Script", ctx.assetReg)) {
        if (ctx.cmdMgr && beforePick != sc->scriptId) {
            ctx.cmdMgr->Execute(
                std::make_unique<SetFieldCommand<AssetID>>(
                    &sc->scriptId, beforePick, sc->scriptId, "Set Script"),
                ctx);
        }
    }

    // ── Class name (optional override; defaults to .cs file stem) ─────────────
    ImGui::TextDisabled("Class");
    ImGui::SameLine();
    char classBuf[256];
    std::strncpy(classBuf, sc->className.c_str(), sizeof(classBuf) - 1);
    classBuf[sizeof(classBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##script_class", classBuf, sizeof(classBuf)))
        sc->className = classBuf;
    if (sc->className.empty() && sc->scriptId.IsValid() && ctx.assetReg) {
        if (const auto* entry = ctx.assetReg->FindByID(sc->scriptId))
            ImGui::TextDisabled("  (auto: %s)", entry->sourcePath.stem().string().c_str());
    }

    // ── Reflected script fields (#74) ─────────────────────────────────────────
    if (!ctx.app) return true;
    auto& ss = ctx.app->GetScriptSystem();

    // Resolve the actual class name used for instantiation: explicit override,
    // else stem of the .cs source. Schema is keyed by exactly this string in
    // ScriptLoader.FindUserScriptType (matches Type.Name).
    std::string effectiveClass = sc->className;
    if (effectiveClass.empty() && sc->scriptId.IsValid() && ctx.assetReg) {
        if (const auto* entry = ctx.assetReg->FindByID(sc->scriptId))
            effectiveClass = entry->sourcePath.stem().string();
    }
    if (effectiveClass.empty()) return true;

    const ScriptClassSchema* schema = ss.GetSchemaFor(effectiveClass);
    if (!schema) {
        ImGui::TextDisabled("(no schema — compile or enter Play once to populate)");
        return true;
    }
    if (schema->fields.empty()) {
        ImGui::TextDisabled("(no public fields)");
        return true;
    }

    ImGui::SeparatorText("Fields");
    bool anyChanged = false;
    for (const auto& f : schema->fields)
        DrawScriptField(*sc, entity, *schema, f, ctx, anyChanged);
    (void)anyChanged;  // currently unused; Undo for script fields lands in #75

    return true;
}

} // namespace StellarAlia::Editor
