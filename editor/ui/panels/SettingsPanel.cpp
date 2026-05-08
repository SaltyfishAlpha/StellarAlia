#include "ui/panels/SettingsPanel.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

void SettingsPanel::OnDraw() {
    ImGuiIO& io = ImGui::GetIO();

    // ── UI scale ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("UI", ImGuiTreeNodeFlags_DefaultOpen)) {
        float scale = io.FontGlobalScale;
        if (ImGui::SliderFloat("Font Scale", &scale, 0.5f, 3.0f, "%.1fx"))
            io.FontGlobalScale = scale;
        ImGui::SameLine();
        if (ImGui::Button("Reset##font"))
            io.FontGlobalScale = 1.0f;
    }

    // ── Display info ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Viewport: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("Frame time: %.2f ms  (%.0f FPS)",
                    1000.0f / io.Framerate, io.Framerate);
    }

    // ── Overlay toggles ───────────────────────────────────────────────────────
    if (m_overlaySettings) {
        if (ImGui::CollapsingHeader("Overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled",        &m_overlaySettings->enabled);
            ImGui::BeginDisabled(!m_overlaySettings->enabled);
            ImGui::Checkbox("Grid",           &m_overlaySettings->drawGrid);
            ImGui::Checkbox("World Axes",     &m_overlaySettings->drawWorldAxes);
            ImGui::Checkbox("Entity Axes",    &m_overlaySettings->drawEntityAxes);
            ImGui::Checkbox("Camera Frustum", &m_overlaySettings->drawCameraFrustum);
            ImGui::Checkbox("Selection Collider", &m_overlaySettings->drawSelectionCollider);
            ImGui::Checkbox("Skeleton Gizmo",     &m_overlaySettings->drawSkeletonGizmo);
            ImGui::Checkbox("Selection Outline",   &m_overlaySettings->drawSelectionAABB);
            ImGui::BeginDisabled(!m_overlaySettings->drawSelectionAABB);
            ImGui::SliderFloat("Outline Width", &m_overlaySettings->outlineWidth, 1.f, 8.f, "%.0f px");
            ImGui::EndDisabled();
            ImGui::Checkbox("Gizmo",          &m_overlaySettings->drawGizmo);
            ImGui::BeginDisabled(!m_overlaySettings->drawGizmo);
            {
                int mode = static_cast<int>(m_overlaySettings->gizmoMode);
                ImGui::RadioButton("Translate (T)", &mode, 0); ImGui::SameLine();
                ImGui::RadioButton("Rotate (R)",    &mode, 1); ImGui::SameLine();
                ImGui::RadioButton("Scale (S)",     &mode, 2);
                m_overlaySettings->gizmoMode = static_cast<GizmoMode>(mode);

                int ws = m_overlaySettings->gizmoWorldSpace ? 1 : 0;
                ImGui::RadioButton("World", &ws, 1); ImGui::SameLine();
                ImGui::RadioButton("Local", &ws, 0);
                m_overlaySettings->gizmoWorldSpace = (ws != 0);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
    }

    // ── Physics debug ─────────────────────────────────────────────────────────
    if (m_physicsSettings) {
        if (ImGui::CollapsingHeader("Physics Debug")) {
            ImGui::Checkbox("Shapes",    &m_physicsSettings->shapes);
            ImGui::Checkbox("AABBs",     &m_physicsSettings->aabbs);
            ImGui::Checkbox("Velocity",  &m_physicsSettings->velocity);
            ImGui::Checkbox("Contacts",  &m_physicsSettings->contacts);
        }
    }
}

} // namespace StellarAlia::Editor
