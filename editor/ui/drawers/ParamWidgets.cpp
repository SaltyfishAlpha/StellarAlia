#include "ui/drawers/ParamWidgets.hpp"

#include "ui/drawers/DrawerHelpers.hpp"   // TrackedFieldEdit

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <variant>

namespace StellarAlia::Editor {

ParamValue DefaultParamValue(const ParamDef& def) {
    switch (def.size) {
        case 16: return glm::vec4(def.defaultValue[0], def.defaultValue[1],
                                  def.defaultValue[2], def.defaultValue[3]);
        case 12: return glm::vec3(def.defaultValue[0], def.defaultValue[1], def.defaultValue[2]);
        case 8:  return glm::vec2(def.defaultValue[0], def.defaultValue[1]);
        default: return def.defaultValue[0];
    }
}

bool DrawReflectedParam(const ParamDef& def, ParamValue& value, const char* label,
                        EditorContext* ctx, const std::string& undoDesc,
                        const std::function<void()>& onApplied) {
    using UI = RHI::ParamUIType;

    // Wrap one ImGui control: tracked (undoable) when a context is supplied.
    auto edit = [&](auto* p, auto drawFn) -> bool {
        if (ctx) return TrackedFieldEdit(p, *ctx, undoDesc, drawFn, onApplied);
        return drawFn(p);
    };

    if (auto* f = std::get_if<float>(&value)) {
        const float lo = def.minValue, hi = def.maxValue;
        if (def.uiType == UI::Range)
            return edit(f, [&](float* p){ return ImGui::SliderFloat(label, p, lo, hi, "%.3f"); });
        const float spd = (hi - lo) * 0.005f;
        return edit(f, [&](float* p){ return ImGui::DragFloat(label, p, spd, lo, hi); });
    }
    if (auto* v2 = std::get_if<glm::vec2>(&value))
        return edit(v2, [&](glm::vec2* p){ return ImGui::DragFloat2(label, glm::value_ptr(*p), 0.01f); });
    if (auto* v3 = std::get_if<glm::vec3>(&value)) {
        if (def.uiType == UI::Color3 || def.uiType == UI::Inferred)
            return edit(v3, [&](glm::vec3* p){ return ImGui::ColorEdit3(label, glm::value_ptr(*p)); });
        return edit(v3, [&](glm::vec3* p){ return ImGui::DragFloat3(label, glm::value_ptr(*p), 0.01f); });
    }
    if (auto* v4 = std::get_if<glm::vec4>(&value)) {
        if (def.uiType == UI::Color4)
            return edit(v4, [&](glm::vec4* p){
                return ImGui::ColorEdit4(label, glm::value_ptr(*p), ImGuiColorEditFlags_Float); });
        return edit(v4, [&](glm::vec4* p){ return ImGui::DragFloat4(label, glm::value_ptr(*p), 0.01f); });
    }
    return false;
}

} // namespace StellarAlia::Editor
