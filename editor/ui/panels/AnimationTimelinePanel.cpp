#include "ui/panels/AnimationTimelinePanel.hpp"

#include "EditorSelection.hpp"
#include "engine/Application.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "resource/AssetRegistry.hpp"
#include "resource/MetaFile.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect — ImSequencer.h's interface uses it
#include <ImSequencer.h>

#include <algorithm>
#include <fstream>

namespace StellarAlia::Editor {

namespace {

constexpr float kTimelineFps = 30.f;   // frame grid only — time stays float seconds

// .sanim sidecars record their source mesh as "source_mesh=<uuid>".
AssetID ReadSourceMesh(const std::filesystem::path& sanimPath) {
    std::ifstream f(sanimPath);
    std::string line;
    while (f && std::getline(f, line)) {
        if (line.rfind("source_mesh=", 0) == 0)
            return AssetID::FromString(line.substr(12));
    }
    return AssetID::Invalid();
}

// Adapter over the panel's clip list for ImSequencer.
struct ClipSequence final : ImSequencer::SequenceInterface {
    std::vector<AnimationTimelinePanel::ClipEntry>* clips = nullptr;
    int frameMax = 1;

    int GetFrameMin() const override { return 0; }
    int GetFrameMax() const override { return frameMax; }
    int GetItemCount() const override { return static_cast<int>(clips->size()); }
    const char* GetItemLabel(int index) const override {
        return (*clips)[index].name.c_str();
    }
    void Get(int index, int** start, int** end, int* type,
             unsigned int* color) override {
        auto& c = (*clips)[index];
        if (start) *start = &c.frameStart;
        if (end)   *end   = &c.frameEnd;
        if (type)  *type  = 0;
        if (color) *color = 0xFF8A5CB4;   // one clip family — single hue
    }
};

} // namespace

AnimationTimelinePanel::AnimationTimelinePanel(EditorContext& ctx)
    : m_ctx(&ctx) {
    isOpen = true;
}

void AnimationTimelinePanel::RefreshClips(const AssetID& meshId) {
    m_clips.clear();
    m_cachedMesh = meshId;
    if (!m_ctx->assetReg || !m_ctx->app || !meshId.IsValid()) return;

    for (const auto* entry : m_ctx->assetReg->EntriesByType("Animation")) {
        if (!(ReadSourceMesh(entry->sourcePath) == meshId)) continue;
        const auto* cooked = m_ctx->app->GetResourceManager().LoadAnimClip(entry->id);
        if (!cooked) continue;
        ClipEntry ce;
        ce.id       = entry->id;
        ce.name     = cooked->clip.name.empty()
                          ? entry->sourcePath.stem().string() : cooked->clip.name;
        ce.duration = cooked->clip.duration;
        ce.frameEnd = std::max(1, static_cast<int>(ce.duration * kTimelineFps + 0.5f));
        m_clips.push_back(std::move(ce));
    }
}

void AnimationTimelinePanel::OnDraw() {
    if (!m_ctx || !m_ctx->selection || !m_ctx->registry || !m_ctx->scene) return;

    const entt::entity selected = m_ctx->selection->GetPrimaryEntity();
    auto& reg = *m_ctx->registry;

    auto* anim = selected != entt::null
                     ? reg.try_get<AnimatorComponent>(selected) : nullptr;
    if (!anim) {
        ImGui::TextDisabled("Select an entity with an Animator component.");
        m_preview = false;
        return;
    }

    const auto* smc = reg.try_get<SkinnedMeshComponent>(selected);
    const AssetID meshId = smc ? smc->meshAsset : AssetID::Invalid();
    if (!(meshId == m_cachedMesh))
        RefreshClips(meshId);

    // Duration of the ASSIGNED clip (may come from another mesh's sidecar and
    // thus be absent from the track list).
    float activeDuration = 0.f;
    int   activeIndex    = -1;
    for (size_t i = 0; i < m_clips.size(); ++i)
        if (m_clips[i].id == anim->clipAsset) {
            activeIndex    = static_cast<int>(i);
            activeDuration = m_clips[i].duration;
        }
    if (activeIndex < 0 && anim->clipAsset.IsValid() && m_ctx->app)
        if (const auto* cooked =
                m_ctx->app->GetResourceManager().LoadAnimClip(anim->clipAsset))
            activeDuration = cooked->clip.duration;

    // ── Transport row ─────────────────────────────────────────────────────────
    const bool editing =
        m_ctx->app && m_ctx->app->GetPlayState() == EnginePlayState::Editing;

    if (!editing) {
        m_preview = false;
        ImGui::TextDisabled("(Play mode — AnimationSystem drives playback)");
    } else {
        if (ImGui::Button(m_preview ? "Stop##tl" : "Preview##tl"))
            m_preview = !m_preview;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::DragFloat("Speed##tl", &anim->speed, 0.01f, 0.f, 10.f);
        ImGui::SameLine();
        ImGui::Checkbox("Loop##tl", &anim->looping);
        ImGui::SameLine();
        ImGui::Text("%.2f / %.2f s", anim->time, activeDuration);

        if (m_preview && anim->clipAsset.IsValid() && activeDuration > 0.f) {
            anim->time += ImGui::GetIO().DeltaTime * anim->speed;
            if (anim->time > activeDuration) {
                if (anim->looping) anim->time = std::fmod(anim->time, activeDuration);
                else { anim->time = activeDuration; m_preview = false; }
            }
            m_ctx->scene->MarkSkinnedMeshDirty();
        }
    }

    if (m_clips.empty()) {
        ImGui::TextDisabled("No animation clips found for this mesh "
                            "(.sanim sidecars next to the model).");
        return;
    }

    // ── Sequencer ─────────────────────────────────────────────────────────────
    ClipSequence seq;
    seq.clips = &m_clips;
    for (const auto& c : m_clips) seq.frameMax = std::max(seq.frameMax, c.frameEnd);

    int currentFrame  = static_cast<int>(anim->time * kTimelineFps + 0.5f);
    int selectedEntry = activeIndex;

    ImSequencer::Sequencer(&seq, &currentFrame, &m_expanded, &selectedEntry,
                           &m_firstFrame, ImSequencer::SEQUENCER_CHANGE_FRAME);

    // Playhead drag → scrub (Editing-state pose convention re-evaluates).
    if (editing) {
        const float newTime = static_cast<float>(currentFrame) / kTimelineFps;
        if (std::fabs(newTime - anim->time) > 1e-4f) {
            anim->time = activeDuration > 0.f
                             ? std::clamp(newTime, 0.f, activeDuration) : newTime;
            m_ctx->scene->MarkSkinnedMeshDirty();
        }
        // Track click → assign that clip (time restarts at 0).
        if (selectedEntry >= 0 && selectedEntry != activeIndex &&
            selectedEntry < static_cast<int>(m_clips.size())) {
            anim->clipAsset = m_clips[selectedEntry].id;
            anim->time      = 0.f;
            m_ctx->scene->MarkSkinnedMeshDirty();
        }
    }
}

} // namespace StellarAlia::Editor
