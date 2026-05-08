#pragma once
#include <imgui.h>

namespace StellarAlia::Editor {

// Classifies a tree-view double-click as short (focus) or long (rename).
// Short: second click released within kLongThreshold seconds.
// Long:  second click held beyond kLongThreshold seconds.
//
// Usage: embed by value in each panel; call OnDoubleClicked() when
// IsMouseDoubleClicked fires, then Update(dt) at the top of OnDraw each frame.
struct DoubleClickClassifier {
    enum class Result { None, Short, Long };
    static constexpr float kLongThreshold = 0.20f;

    void OnDoubleClicked() { m_tracking = true; m_holdTime = 0.f; }

    Result Update(float dt) {
        if (!m_tracking) return Result::None;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_holdTime += dt;
            return Result::None;
        }
        m_tracking = false;
        return m_holdTime < kLongThreshold ? Result::Short : Result::Long;
    }

    bool IsTracking() const { return m_tracking; }

private:
    bool  m_tracking = false;
    float m_holdTime = 0.f;
};

} // namespace StellarAlia::Editor
