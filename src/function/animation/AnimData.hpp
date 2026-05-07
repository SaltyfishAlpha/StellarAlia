#pragma once
// Re-export animation data types from the resource layer.
// The canonical definitions live in resource/types/AnimData.hpp (StellarAlia::Resource).
#include "resource/types/AnimData.hpp"

namespace StellarAlia {
    using Resource::BoneInfo;
    using Resource::AnimChannel;
    using Resource::AnimClip;
    using Resource::SkinVertex;
} // namespace StellarAlia
