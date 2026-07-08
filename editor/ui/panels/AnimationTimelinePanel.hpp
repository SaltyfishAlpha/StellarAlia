#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorContext.hpp"
#include "core/asset/AssetID.hpp"

#include <string>
#include <vector>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// AnimationTimelinePanel (#83, absorbed X-3) — ImSequencer-based clip timeline
// for the selected entity's Animator.
//
// Tracks = every Animation asset cooked from the entity's mesh (sidecar
// source_mesh match). Clicking a track assigns the clip; dragging the playhead
// scrubs Animator.time — the Editing-state pose convention (EvaluateAll(-1)
// via the skinned-dirty hook) makes the viewport follow live. "Preview" plays
// the clip editor-side (Editing only; PIE playback stays AnimationSystem's).
// ─────────────────────────────────────────────────────────────────────────────
class AnimationTimelinePanel : public IEditorWindow {
public:
    explicit AnimationTimelinePanel(EditorContext& ctx);

    std::string_view GetName() const override { return "Animation"; }
    void OnDraw() override;

    // Public: the file-local ImSequencer adapter reads these.
    struct ClipEntry {
        AssetID     id;
        std::string name;
        float       duration   = 0.f;
        int         frameStart = 0;   // ImSequencer wants mutable int* storage
        int         frameEnd   = 1;
    };

private:
    void RefreshClips(const AssetID& meshId);

    EditorContext*         m_ctx = nullptr;
    AssetID                m_cachedMesh;
    std::vector<ClipEntry> m_clips;

    bool m_preview    = false;   // editor-side playback toggle
    int  m_firstFrame = 0;       // ImSequencer horizontal scroll state
    bool m_expanded   = true;
};

} // namespace StellarAlia::Editor
