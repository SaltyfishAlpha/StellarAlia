#include "ui/panels/PerformancePanel.hpp"

#include <imgui.h>
#include "platform/PlatformMemory.hpp"

namespace StellarAlia::Editor {

void PerformancePanel::OnDraw() {
    constexpr double kMB = 1.0 / (1024.0 * 1024.0);
    const ImGuiIO& io = ImGui::GetIO();

    // ── Hardware ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hardware", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_device) {
            const auto gpu = m_device->GetDeviceName();
            ImGui::Text("GPU: %.*s", static_cast<int>(gpu.size()), gpu.data());
        } else {
            ImGui::TextDisabled("GPU: (unavailable)");
        }
        static const std::string cpuName = Platform::GetCpuName();
        ImGui::Text("CPU: %s", cpuName.c_str());
    }

    // ── Display ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Viewport:   %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("Frame time: %.2f ms  (%.0f FPS)",
                    1000.0f / io.Framerate, io.Framerate);
    }

    // ── Memory ────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        // GPU VRAM
        ImGui::SeparatorText("GPU VRAM");
        if (m_renderGraph) {
            const auto& mem = m_renderGraph->GetLastMemoryStats();
            if (mem.gpuBudgetBytes > 0) {
                const double usedMB   = static_cast<double>(mem.gpuUsedBytes)   * kMB;
                const double budgetMB = static_cast<double>(mem.gpuBudgetBytes) * kMB;
                const float  fraction = static_cast<float>(mem.gpuUsedBytes) /
                                        static_cast<float>(mem.gpuBudgetBytes);
                ImGui::ProgressBar(fraction, ImVec2(-1, 0));
                ImGui::Text("Used:     %.1f / %.1f MB  (%.0f%%)",
                            usedMB, budgetMB, fraction * 100.0f);
            } else {
                ImGui::Text("Used:     %.1f MB",
                            static_cast<double>(mem.gpuUsedBytes) * kMB);
            }
            ImGui::Text("Textures: %.1f MB",
                        static_cast<double>(mem.gpuTextureBytes) * kMB);
            ImGui::Text("Buffers:  %.1f MB",
                        static_cast<double>(mem.gpuBufferBytes) * kMB);
        } else {
            ImGui::TextDisabled("(no render graph)");
        }

        // CPU RAM
        ImGui::SeparatorText("CPU RAM");
        const uint64_t cpuBytes = Platform::GetProcessMemoryBytes();
        if (cpuBytes > 0)
            ImGui::Text("Process:  %.1f MB", static_cast<double>(cpuBytes) * kMB);
        else
            ImGui::TextDisabled("Process:  (unavailable)");
    }

    // ── Render Stats ──────────────────────────────────────────────────────────
    if (m_renderGraph) {
        if (ImGui::CollapsingHeader("Render Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            const RGStats& s = m_renderGraph->GetLastFrameStats();

            // Summary — always visible
            ImGui::Text("Imported:  %u  (%.1f MB)",
                        s.importedCount,
                        static_cast<double>(s.importedBytesLogical) * kMB);

            const double logMB  = static_cast<double>(s.transientBytesLogical)  * kMB;
            const double physMB = static_cast<double>(s.transientBytesPhysical) * kMB;
            ImGui::Text("Transient: %u logical / %u physical",
                        s.transientCount, s.physicalSlotCount);
            ImGui::Text("Logical:   %.2f MB", logMB);
            if (s.transientBytesLogical > 0 &&
                s.transientBytesPhysical < s.transientBytesLogical) {
                const double savedMB  = logMB - physMB;
                const double savedPct = savedMB / logMB * 100.0;
                ImGui::Text("Physical:  %.2f MB  (saved %.2f MB, %.1f%%)",
                            physMB, savedMB, savedPct);
            } else {
                ImGui::Text("Physical:  %.2f MB", physMB);
            }

            // ── Buffer stats ─────────────────────────────────────────────────────
            {
                ImGui::Separator();
                ImGui::Text("Buffers imported:  %u", s.importedBufferCount);
                const double logBufMB  = static_cast<double>(s.transientBufferBytesLogical)  * kMB;
                const double physBufMB = static_cast<double>(s.transientBufferBytesPhysical) * kMB;
                ImGui::Text("Buffers transient: %u logical / %u physical",
                            s.transientBufferCount, s.physicalBufferSlotCount);
                if (s.transientBufferBytesLogical > 0 &&
                    s.transientBufferBytesPhysical < s.transientBufferBytesLogical) {
                    const double savedMB  = logBufMB - physBufMB;
                    const double savedPct = savedMB / logBufMB * 100.0;
                    ImGui::Text("Buffer logical:  %.3f MB", logBufMB);
                    ImGui::Text("Buffer physical: %.3f MB  (saved %.3f MB, %.1f%%)",
                                physBufMB, savedMB, savedPct);
                } else {
                    ImGui::Text("Buffer logical:  %.3f MB", logBufMB);
                }

                if (ImGui::TreeNode("Buffer Details")) {
                    ImGui::Columns(3, "rg_buf_detail_cols", true);
                    ImGui::TextUnformatted("Name");  ImGui::NextColumn();
                    ImGui::TextUnformatted("Bytes"); ImGui::NextColumn();
                    ImGui::TextUnformatted("Slot");  ImGui::NextColumn();
                    ImGui::Separator();
                    for (const auto& e : s.bufferEntries) {
                        ImGui::Text("%s", e.name.c_str()); ImGui::NextColumn();
                        ImGui::Text("%llu", static_cast<unsigned long long>(e.bytes)); ImGui::NextColumn();
                        if (e.slotIndex >= 0) ImGui::Text("%d", e.slotIndex);
                        else                  ImGui::TextUnformatted("-");
                        ImGui::NextColumn();
                    }
                    ImGui::Columns(1);
                    ImGui::TreePop();
                }
            }

            // Per-texture detail table
            if (!s.entries.empty() && ImGui::TreeNode("Details")) {
                ImGui::Columns(5, "rg_detail_cols", true);
                ImGui::TextUnformatted("Name");   ImGui::NextColumn();
                ImGui::TextUnformatted("Size");   ImGui::NextColumn();
                ImGui::TextUnformatted("Format"); ImGui::NextColumn();
                ImGui::TextUnformatted("MB");     ImGui::NextColumn();
                ImGui::TextUnformatted("Slot");   ImGui::NextColumn();
                ImGui::Separator();
                for (const auto& e : s.entries) {
                    ImGui::Text("%s", e.name.c_str());  ImGui::NextColumn();
                    if (e.mipLevels > 1)
                        ImGui::Text("%ux%u (%u mips)", e.width, e.height, e.mipLevels);
                    else
                        ImGui::Text("%ux%u", e.width, e.height);
                    ImGui::NextColumn();
                    ImGui::Text("%s", e.formatStr ? e.formatStr : "?");
                    ImGui::NextColumn();
                    ImGui::Text("%.2f", static_cast<double>(e.bytes) * kMB);
                    ImGui::NextColumn();
                    if (e.slotIndex >= 0) ImGui::Text("%d", e.slotIndex);
                    else                  ImGui::TextUnformatted("-");
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
                ImGui::TreePop();
            }
        }
    }
}

} // namespace StellarAlia::Editor
