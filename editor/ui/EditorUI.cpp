#include "ui/EditorUI.hpp"
#include "ui/IEditorWindow.hpp"

#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanCommandList.hpp"
#include "core/logs/Log.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>
#include <volk.h>

#include <string>

namespace StellarAlia::Editor {

bool EditorUI::Init(GLFWwindow* window, RHI::VulkanDevice* device) {
    m_device = device;
    auto ctx = device->GetImGuiContext();

    // ── ImGui context ─────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // ── GLFW backend ──────────────────────────────────────────────────────────
    ImGui_ImplGlfw_InitForVulkan(window, /*install_callbacks=*/true);

    // ── Vulkan backend (dynamic rendering, Vulkan 1.3) ────────────────────────
    // VK_FORMAT_B8G8R8A8_UNORM is the most common desktop swapchain format.
    // TODO: query GetSwapchainFormat() from device and convert to VkFormat.
    static const VkFormat swapFmt = VK_FORMAT_B8G8R8A8_UNORM;

    // When VK_NO_PROTOTYPES / IMGUI_IMPL_VULKAN_NO_PROTOTYPES is defined (volk mode),
    // ImGui cannot resolve Vulkan functions on its own. Provide a loader that
    // delegates to volk's vkGetInstanceProcAddr with the real instance.
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_3,
        [](const char* name, void* userData) -> PFN_vkVoidFunction {
            return vkGetInstanceProcAddr(
                *static_cast<VkInstance*>(userData), name);
        }, &ctx.instance);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion          = VK_API_VERSION_1_3;
    initInfo.Instance            = ctx.instance;
    initInfo.PhysicalDevice      = ctx.physicalDevice;
    initInfo.Device              = ctx.device;
    initInfo.QueueFamily         = ctx.graphicsFamily;
    initInfo.Queue               = ctx.graphicsQueue;
    // Let ImGui create and manage its own descriptor pool.
    initInfo.DescriptorPoolSize  = 64;
    initInfo.MinImageCount       = ctx.swapchainMinImageCount;
    initInfo.ImageCount          = ctx.swapchainImageCount;
    initInfo.UseDynamicRendering = true;

#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapFmt;
#endif

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        SA_LOG_ERROR("EditorUI: ImGui_ImplVulkan_Init failed");
        ImGui::DestroyContext();
        return false;
    }

    m_initialized = true;
    SA_LOG_INFO("EditorUI: initialised (docking enabled)");
    return true;
}

void EditorUI::Shutdown() {
    if (!m_initialized) return;
    m_initialized = false;

    for (auto& w : m_windows)
        w->OnClose();
    m_windows.clear();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_device = nullptr;
    SA_LOG_INFO("EditorUI: shut down");
}

void EditorUI::RegisterWindow(std::unique_ptr<IEditorWindow> panel) {
    panel->OnOpen();
    m_windows.push_back(std::move(panel));
}

void EditorUI::NewFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Full-screen DockSpace so panels can dock anywhere.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar            | ImGuiWindowFlags_NoCollapse       |
        ImGuiWindowFlags_NoResize              | ImGuiWindowFlags_NoMove           |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus       |
        ImGuiWindowFlags_NoDocking             | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.f, 0.f),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void EditorUI::DrawPanels() {
    // ── Main menu bar — Windows submenu lets users reopen closed panels ────────
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            for (auto& w : m_windows) {
                bool open = w->isOpen;
                if (ImGui::MenuItem(std::string(w->GetName()).c_str(), nullptr, open)) {
                    w->isOpen = !open;
                    if (w->isOpen) w->OnOpen();
                    else           w->OnClose();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ── Panel windows ─────────────────────────────────────────────────────────
    for (auto& w : m_windows) {
        if (!w->isOpen) continue;
        bool open = w->isOpen;
        if (ImGui::Begin(std::string(w->GetName()).c_str(), &open))
            w->OnDraw();
        ImGui::End();
        if (!open && w->isOpen) {
            w->isOpen = false;
            w->OnClose();
        }
    }
}

void EditorUI::Render(RHI::IRHICommandList* cmdList) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->TotalVtxCount == 0)
        return;

    // Resolve raw Vulkan handles.
    auto* vkCmdList = static_cast<RHI::VulkanCommandList*>(cmdList);
    VkCommandBuffer cmd      = vkCmdList->GetVkCommandBuffer();
    VkImageView     swapView = m_device->GetVkImageView(m_device->GetSwapchainTexture());
    uint32_t        sw       = m_device->GetSwapchainWidth();
    uint32_t        sh       = m_device->GetSwapchainHeight();

    // Open a dynamic rendering scope targeting the swapchain image.
    // The render graph has left the image in COLOR_ATTACHMENT_OPTIMAL.
    VkRenderingAttachmentInfo colorAtt{};
    colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAtt.imageView   = swapView;
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;    // preserve 3D scene
    colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent    = { sw, sh };
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &colorAtt;

    vkCmdBeginRendering(cmd, &ri);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    vkCmdEndRendering(cmd);
}

void EditorUI::OnSwapchainResize(uint32_t minImageCount) {
    ImGui_ImplVulkan_SetMinImageCount(minImageCount);
}

} // namespace StellarAlia::Editor
