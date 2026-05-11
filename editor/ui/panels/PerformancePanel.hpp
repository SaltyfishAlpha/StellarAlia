#pragma once
#include "ui/IEditorWindow.hpp"
#include "EditorContext.hpp"
#include "function/render_graph/RenderGraph.hpp"
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia::Editor {

class PerformancePanel : public IEditorWindow {
public:
    explicit PerformancePanel(EditorContext& ctx);

    std::string_view GetName() const override { return "Performance"; }
    void OnDraw() override;

private:
    const RenderGraph* m_renderGraph = nullptr;
    RHI::IRHIDevice*   m_device      = nullptr;
};

} // namespace StellarAlia::Editor
