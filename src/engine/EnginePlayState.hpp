#pragma once

namespace StellarAlia {

enum class EnginePlayState {
    Editing,  // scene is live-editable; animation systems are paused
    Playing,  // game simulation running; animation systems tick each frame
    Paused,   // game simulation suspended; last deformed frame held on GPU
};

} // namespace StellarAlia
