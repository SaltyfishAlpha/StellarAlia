using System;
using StellarAlia;

/// Demonstrates the runtime PostProcess script API (Issue #47).
///
/// Attach to any entity in the scene, then enter Play mode:
///   1 — toggle Vignette
///   2 — toggle Chromatic Aberration
///   3 — toggle Film Grain
///   4 — toggle the time-driven animation (vignette pulse + CA breath)
///   R — restore all three effects to their default-off state
///
/// Every toggle and the current animated values are logged to Console.
/// Stop Play mode to verify the original (.sascene) values are reloaded.
public class PostProcessTest : ScriptBase
{
    /// Speed of the vignette intensity pulse (cycles per second).
    float pulseHz = 0.4f;

    /// Speed of the CA strength breath (cycles per second).
    float caBreathHz = 0.7f;

    bool animate = true;

    public override void OnStart()
    {
        Debug.Log("=== PostProcessTest ===");
        Debug.Log("Press 1/2/3 to toggle Vignette / ChromaticAberration / FilmGrain.");
        Debug.Log("Press 4 to toggle animation, R to reset.");
        LogState();
    }

    public override void OnUpdate(float dt)
    {
        if (Input.IsKeyJustPressed(Key.Num1)) {
            PostProcess.Vignette.Enabled = !PostProcess.Vignette.Enabled;
            Debug.Log($"Vignette.Enabled = {PostProcess.Vignette.Enabled}");
        }
        if (Input.IsKeyJustPressed(Key.Num2)) {
            PostProcess.ChromaticAberration.Enabled = !PostProcess.ChromaticAberration.Enabled;
            Debug.Log($"ChromaticAberration.Enabled = {PostProcess.ChromaticAberration.Enabled}");
        }
        if (Input.IsKeyJustPressed(Key.Num3)) {
            PostProcess.FilmGrain.Enabled = !PostProcess.FilmGrain.Enabled;
            Debug.Log($"FilmGrain.Enabled = {PostProcess.FilmGrain.Enabled}");
        }
        if (Input.IsKeyJustPressed(Key.Num4)) {
            animate = !animate;
            Debug.Log($"animation = {animate}");
        }
        if (Input.IsKeyJustPressed(Key.R)) {
            ResetAll();
            LogState();
        }

        if (animate) {
            float t = Time.TotalTime;
            // Vignette intensity pulses 0.25 .. 0.65
            PostProcess.Vignette.Intensity =
                0.45f + MathF.Sin(t * pulseHz * MathF.Tau) * 0.20f;
            // CA strength breathes 0.5 .. 2.5
            PostProcess.ChromaticAberration.Strength =
                1.5f + MathF.Sin(t * caBreathHz * MathF.Tau) * 1.0f;
            // FilmGrain.Size ramps slowly between 1.0 and 3.0 via PingPong
            PostProcess.FilmGrain.Size =
                1.0f + Mathf.PingPong(t * 0.5f, 1.0f) * 2.0f;
        }
    }

    void ResetAll()
    {
        PostProcess.Vignette.Enabled              = false;
        PostProcess.Vignette.Intensity            = 0.4f;
        PostProcess.Vignette.Smoothness           = 0.6f;
        PostProcess.ChromaticAberration.Enabled   = false;
        PostProcess.ChromaticAberration.Strength  = 0.5f;
        PostProcess.FilmGrain.Enabled             = false;
        PostProcess.FilmGrain.Intensity           = 0.1f;
        PostProcess.FilmGrain.Size                = 1.6f;
    }

    void LogState()
    {
        Debug.Log(
            $"Vignette: enabled={PostProcess.Vignette.Enabled} " +
            $"intensity={PostProcess.Vignette.Intensity:F3} " +
            $"smoothness={PostProcess.Vignette.Smoothness:F3}");
        Debug.Log(
            $"CA: enabled={PostProcess.ChromaticAberration.Enabled} " +
            $"strength={PostProcess.ChromaticAberration.Strength:F3}");
        Debug.Log(
            $"FilmGrain: enabled={PostProcess.FilmGrain.Enabled} " +
            $"intensity={PostProcess.FilmGrain.Intensity:F3} " +
            $"size={PostProcess.FilmGrain.Size:F3}");
    }
}
