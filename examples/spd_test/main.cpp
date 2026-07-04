// spd_test — Issue #94 SPD single-pass downsampler validation.
//
// Non-graphical numerical unit test: uploads a known R32F source, runs
// spd_downsample.comp once via ImmediateCompute (writing the full mip chain into
// a mip-chain texture through the new storage-image ARRAY-element binding), reads
// the chain back, and asserts each mip matches a CPU box-average reference.
//
// The 256x256 / 8-mip case exercises BOTH kernel paths: the tile-local reduction
// (mip1..mip6) and the global atomic last-workgroup step (mip7..mip8). Validates the
// infra #94 adds — WriteDescriptorStorageImageArrayMip + the single-pass kernel —
// with no on-screen output. A GLFW window exists only because VulkanDevice::Create
// requires a surface; no frame loop is entered.

#include "core/logs/Log.hpp"
#include "function/material/ComputeProgram.hpp"
#include "platform/rhi/IRHICommandList.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "SpdTestPath.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("LoadSpv: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

static ShaderReflection LoadRefl(const std::string& path) {
    ShaderReflection refl;
    if (!ShaderReflectionIO::LoadFromFile(path, refl))
        SA_LOG_ERROR("LoadRefl: cannot open '{}'", path);
    return refl;
}

// CPU 2x2 reductions matching the kernels' SPD_REDUCE: box-average (spd_downsample.comp
// default) and min (hiz_spd.comp, Issue #89 Hi-Z).
static std::vector<float> BoxReduce(const std::vector<float>& src, uint32_t w, uint32_t h) {
    const uint32_t ow = w / 2, oh = h / 2;
    std::vector<float> out(static_cast<size_t>(ow) * oh);
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x)
            out[y * ow + x] = 0.25f * (src[(2 * y)     * w + 2 * x]     +
                                       src[(2 * y)     * w + 2 * x + 1] +
                                       src[(2 * y + 1) * w + 2 * x]     +
                                       src[(2 * y + 1) * w + 2 * x + 1]);
    return out;
}
static std::vector<float> MinReduce(const std::vector<float>& src, uint32_t w, uint32_t h) {
    const uint32_t ow = w / 2, oh = h / 2;
    std::vector<float> out(static_cast<size_t>(ow) * oh);
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x)
            out[y * ow + x] = std::min(std::min(src[(2 * y)     * w + 2 * x],
                                                src[(2 * y)     * w + 2 * x + 1]),
                                       std::min(src[(2 * y + 1) * w + 2 * x],
                                                src[(2 * y + 1) * w + 2 * x + 1]));
    return out;
}

// Runs an SPD kernel (`stem`) on a W×H source producing `MIPS` levels, returns true if
// every mip matches the CPU reference (min when `useMin`, else box-average). MIPS>6
// exercises the global atomic step.
static bool RunCase(IRHIDevice* device, uint32_t W, uint32_t H, uint32_t MIPS,
                    const char* stem, bool useMin) {
    bool pass = true;
    {
        // ── Source: deterministic smooth pattern ─────────────────────────────
        std::vector<float> src(static_cast<size_t>(W) * H);
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x)
                src[y * W + x] = std::sin(x * 0.05f) * std::cos(y * 0.03f) + float(x + y) * 0.01f;

        RHITextureDesc sd{};
        sd.width = W; sd.height = H;
        sd.format = RHIFormat::R32F;
        // UnorderedAccess: hiz_spd (SPD_SRC_IMAGE) reads mip0 via imageLoad (GENERAL);
        // Sampled: spd_downsample reads it via a sampler (SHADER_READ). Support both.
        sd.usage  = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess
                  | RHITextureUsage::CopyDst;
        sd.debugName = "SpdSrc";
        RHITextureHandle srcTex = device->CreateTexture(sd);
        device->UploadTextureData(srcTex, src.data(), src.size() * sizeof(float));

        // ── Dest mip-chain: mip0..mipMIPS (mip0 unused), UAV + Sampled + CopySrc ──
        RHITextureDesc dd{};
        dd.width = W; dd.height = H;
        dd.mipLevels = MIPS + 1;
        dd.format = RHIFormat::R32F;
        // Sampled too: the real Hi-Z chain is sampled by the SSR traversal, and
        // ReadbackTextureMips transitions through SHADER_READ_ONLY (needs SAMPLED).
        dd.usage  = RHITextureUsage::UnorderedAccess | RHITextureUsage::Sampled
                  | RHITextureUsage::CopySrc;
        dd.debugName = "SpdChain";
        RHITextureHandle dstTex = device->CreateTexture(dd);

        // ── Global atomic counter SSBO (zero-initialised; kernel self-resets) ──
        RHIBufferDesc cd{};
        cd.size       = sizeof(uint32_t);
        cd.usage      = RHIBufferUsage::Storage | RHIBufferUsage::CopyDst;
        cd.cpuVisible = true;
        cd.debugName  = "SpdCounter";
        RHIBufferHandle counter = device->CreateBuffer(cd);
        const uint32_t zero = 0;
        device->UploadBufferData(counter, &zero, sizeof(zero));

        // ── Program + descriptor set (t_src @0, u_mips[] @1, counter @2) ──────
        const std::string dir = SpdTest::BUILTIN_SHADER_DIR;
        ComputeProgram spd;
        if (!spd.Load(device, {LoadSpv(dir + "/" + stem + ".comp.spv"),
                                     LoadRefl(dir + "/" + stem + ".comp.refl")})) {
            SA_LOG_CRITICAL("SpdTest: ComputeProgram::Load failed");
            device->WaitIdle();
            return false;
        }

        RHIDescSetHandle ds = device->AllocateDescriptorSet(spd.GetLayout(0));
        if (useMin) device->WriteDescriptorStorageImage(ds, 0, srcTex);  // hiz_spd: imageLoad
        else        device->WriteDescriptorTexture     (ds, 0, srcTex);  // spd_downsample: sampler
        for (uint32_t i = 0; i < MIPS; ++i)
            device->WriteDescriptorStorageImageArrayMip(ds, 1, /*arrayElem=*/i,
                                                        dstTex, /*mip=*/i + 1);
        device->WriteDescriptorBuffer(ds, 2, counter);

        RHIPipelineHandle pipe = spd.GetPipeline(device);
        if (!pipe.IsValid()) { SA_LOG_CRITICAL("SpdTest: pipeline invalid"); device->WaitIdle(); return false; }

        const uint32_t groupsX = (W + 63) / 64, groupsY = (H + 63) / 64;
        struct PC { int32_t srcW, srcH, mipCount, numWorkGroups; }
            pc{int32_t(W), int32_t(H), int32_t(MIPS), int32_t(groupsX * groupsY)};

        device->ImmediateCompute([&](IRHICommandList* cmd) {
            cmd->TransitionTexture(dstTex, RHIResourceState::Undefined,
                                           RHIResourceState::UnorderedAccess);
            // hiz_spd reads src via imageLoad → needs GENERAL (upload left it ShaderRead).
            if (useMin) cmd->TransitionTexture(srcTex, RHIResourceState::ShaderRead,
                                                       RHIResourceState::UnorderedAccess);
            cmd->SetComputePipeline(pipe);
            cmd->SetDescriptorSet(0, ds);
            cmd->SetPushConstants(&pc, sizeof(pc), RHIShaderStage::Compute);
            cmd->Dispatch(groupsX, groupsY, 1);
            // Leave in ShaderRead — ReadbackTextureMips expects that layout.
            cmd->TransitionTexture(dstTex, RHIResourceState::UnorderedAccess,
                                           RHIResourceState::ShaderRead);
        });

        // ── Readback mip0..mipMIPS ────────────────────────────────────────────
        std::vector<std::vector<float>> mipData(MIPS + 1);
        std::vector<IRHIDevice::MipReadback> rb(MIPS + 1);
        for (uint32_t m = 0; m <= MIPS; ++m) {
            const uint32_t mw = std::max(1u, W >> m), mh = std::max(1u, H >> m);
            mipData[m].resize(static_cast<size_t>(mw) * mh);
            rb[m] = {mipData[m].data(), mipData[m].size() * sizeof(float)};
        }
        device->ReadbackTextureMips(dstTex, rb);

        // ── CPU reference + compare mip1..mipMIPS ─────────────────────────────
        std::vector<float> ref = src;
        uint32_t rw = W, rh = H;
        for (uint32_t m = 1; m <= MIPS; ++m) {
            ref = useMin ? MinReduce(ref, rw, rh) : BoxReduce(ref, rw, rh);
            rw /= 2; rh /= 2;

            float maxErr = 0.f;
            for (size_t i = 0; i < ref.size(); ++i)
                maxErr = std::max(maxErr, std::abs(ref[i] - mipData[m][i]));

            const bool ok = maxErr < 1e-4f;
            SA_LOG_INFO("SpdTest: {} {}x{} mip{} ({}x{}) maxErr={:.3e} {}{}", stem, W, H, m,
                        rw, rh, maxErr, ok ? "PASS" : "FAIL", m > 6 ? " [global step]" : "");
            if (!ok) pass = false;
        }

        device->WaitIdle();
        spd.Unload(device);
        device->DestroyBuffer(counter);
        device->DestroyTexture(srcTex);
        device->DestroyTexture(dstTex);
    }
    return pass;
}

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== SPD Test (Issue #94) ===");

    auto window = GLFWWindow::Create(WindowDesc{256, 256, "SpdTest"});
    if (!window) { SA_LOG_CRITICAL("window create failed"); return 1; }

    auto device = VulkanDevice::Create({
        .windowHandle    = {window->GetNativeHandle()},
        .swapchainWidth  = window->GetWidth(),
        .swapchainHeight = window->GetHeight(),
        .vsync           = true,
        .enableValidation = true,
    });
    if (!device) { SA_LOG_CRITICAL("device create failed"); return 1; }

    bool ok = true;
    // Average SPD (Issue #94): local + global atomic step.
    ok &= RunCase(device.get(), 256, 256, 8, "spd_downsample", /*useMin=*/false);
    ok &= RunCase(device.get(), 1024, 1024, 10, "spd_downsample", /*useMin=*/false);
    // Min SPD (Issue #89 Hi-Z): same paths, min reduce.
    ok &= RunCase(device.get(), 256, 256, 8, "hiz_spd", /*useMin=*/true);
    ok &= RunCase(device.get(), 1024, 1024, 10, "hiz_spd", /*useMin=*/true);

    device->WaitIdle();
    device.reset();
    window.reset();
    SA_LOG_INFO(ok ? "=== SPD Test: ALL CASES PASS ===" : "=== SPD Test: FAILURES ===");
    Core::Log::Shutdown();
    return ok ? 0 : 1;
}
