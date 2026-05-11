namespace StellarAlia;

/// <summary>
/// Base class for all user scripts. Attach to an Entity via ScriptComponent.
/// </summary>
public abstract class ScriptBase
{
    // Set by ScriptLoader before any lifecycle call.
    internal ulong EntityId = 0;

    protected Entity Self => new Entity(EntityId);

    // ── Lifecycle — override as needed ───────────────────────────────────────

    /// Called once when the script is first attached (other scripts may not be ready yet).
    public virtual void OnAttach() { }

    /// Called after ALL entities have finished OnAttach — safe to call Entity.Find().
    public virtual void OnStart() { }

    /// Called each physics step (fixed dt = 1/60 s). Use for physics-driven movement.
    public virtual void OnFixedUpdate(float dt) { }

    /// Called each variable-rate frame. Use for input response and game logic.
    public virtual void OnUpdate(float dt) { }

    /// Called after all OnUpdate calls, before UpdateTransforms. Use for camera follow.
    public virtual void OnLateUpdate(float dt) { }

    /// Called when Play is stopped (before OnDetach).
    public virtual void OnStop() { }

    /// Called when the script is removed or the scene is unloaded.
    public virtual void OnDetach() { }
}
