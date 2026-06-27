using StellarAlia;

/// Toggles between "Gameplay" and "Menu" InputMaps when Escape is pressed
/// (Issue #71 Phase 3a demo).
///
/// Attach to any entity in the scene. The raw-device Input.IsKeyJustPressed
/// is used for the toggle key on purpose — it bypasses the map stack, so the
/// toggle works regardless of which map is currently active. Otherwise we'd
/// need a Cancel action in both Gameplay and Menu maps.
///
/// Expected .sainputmap layout in the project:
///   - Controls.sainputmap  (name="Gameplay")  ← pushed automatically on PIE entry
///   - Menu.sainputmap      (name="Menu")
///
/// Verify it works:
///   - Press Play → Console shows "active=Gameplay"
///   - Press Esc  → Console shows "active=Menu", PlayerController stops moving
///                  (Move action only exists in Gameplay)
///   - Press Esc  → Console shows "active=Gameplay", PlayerController resumes.
public class PauseToggle : ScriptBase
{
    public override void OnStart()
    {
        Debug.Log($"PauseToggle: active map at start = '{InputMap.GetActive()}'");
    }

    public override void OnUpdate(float dt)
    {
        if (!Input.IsKeyJustPressed(Key.Escape)) return;

        if (InputMap.GetActive() == "Menu") {
            InputMap.Pop();
            Debug.Log($"PauseToggle: resumed → active='{InputMap.GetActive()}'");
        } else {
            if (!InputMap.Push("Menu"))
                Debug.Log("PauseToggle: Menu map not registered — create assets/inputmaps/Menu.sainputmap");
            else
                Debug.Log($"PauseToggle: paused → active='{InputMap.GetActive()}'");
        }
    }
}
