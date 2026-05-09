#pragma once
#include "ui/IEditorWindow.hpp"
#include "function/render_graph/RenderGraph.hpp"

namespace StellarAlia::Editor {

class PerformancePanel : public IEditorWindow {
public:
    explicit PerformancePanel(const RenderGraph* renderGraph = nullptr)
        : m_renderGraph(renderGraph)
    {
        isOpen = false;
    }

    std::string_view GetName() const override { return "Performance"; }
    void OnDraw() override;

private:
    const RenderGraph* m_renderGraph = nullptr;
};

} // namespace StellarAlia::Editor
