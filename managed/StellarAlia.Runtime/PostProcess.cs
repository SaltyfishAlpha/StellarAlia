namespace StellarAlia;

/// <summary>
/// Runtime access to the active scene's screen-modification effects
/// (Issue #47).  Mirrors the Editor's PostProcess panel — setters mutate
/// WorldSettings.pp and live-apply to the renderer immediately.
/// </summary>
/// <example>
/// // Pulse the vignette with time:
/// void Update() {
///     PostProcess.Vignette.Enabled   = true;
///     PostProcess.Vignette.Intensity = 0.4f + Mathf.Sin(Time.TotalTime) * 0.1f;
/// }
/// </example>
public static class PostProcess
{
    /// <summary>Radial darkening around the screen edges.</summary>
    public static class Vignette
    {
        /// <summary>Toggle for this effect layer.</summary>
        public static bool Enabled {
            get => NativeApi.SA_PostProcess_GetVignetteEnabled() != 0;
            set => NativeApi.SA_PostProcess_SetVignetteEnabled(value ? 1 : 0);
        }
        /// <summary>Radius at which the vignette starts darkening (0..1, normalized to the half-diagonal).</summary>
        public static float Intensity {
            get => NativeApi.SA_PostProcess_GetVignetteIntensity();
            set => NativeApi.SA_PostProcess_SetVignetteIntensity(value);
        }
        /// <summary>Falloff width — larger = softer edge (0.01..1).</summary>
        public static float Smoothness {
            get => NativeApi.SA_PostProcess_GetVignetteSmoothness();
            set => NativeApi.SA_PostProcess_SetVignetteSmoothness(value);
        }
    }

    /// <summary>Radial RGB offset that mimics a real lens (no offset at the centre, max at the edges).</summary>
    public static class ChromaticAberration
    {
        /// <summary>Toggle for this effect layer.</summary>
        public static bool Enabled {
            get => NativeApi.SA_PostProcess_GetCAEnabled() != 0;
            set => NativeApi.SA_PostProcess_SetCAEnabled(value ? 1 : 0);
        }
        /// <summary>RGB shift magnitude (0..5). 1.0 ≈ a few pixels at 1080p.</summary>
        public static float Strength {
            get => NativeApi.SA_PostProcess_GetCAStrength();
            set => NativeApi.SA_PostProcess_SetCAStrength(value);
        }
    }

    /// <summary>Per-frame hash noise attenuated in bright regions to mimic film stock.</summary>
    public static class FilmGrain
    {
        /// <summary>Toggle for this effect layer.</summary>
        public static bool Enabled {
            get => NativeApi.SA_PostProcess_GetFilmGrainEnabled() != 0;
            set => NativeApi.SA_PostProcess_SetFilmGrainEnabled(value ? 1 : 0);
        }
        /// <summary>Noise amplitude (0..0.3).</summary>
        public static float Intensity {
            get => NativeApi.SA_PostProcess_GetFilmGrainIntensity();
            set => NativeApi.SA_PostProcess_SetFilmGrainIntensity(value);
        }
        /// <summary>Grain tile scale; larger = chunkier noise (0.5..5).</summary>
        public static float Size {
            get => NativeApi.SA_PostProcess_GetFilmGrainSize();
            set => NativeApi.SA_PostProcess_SetFilmGrainSize(value);
        }
    }
}
