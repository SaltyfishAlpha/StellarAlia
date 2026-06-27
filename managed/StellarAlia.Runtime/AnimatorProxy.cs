namespace StellarAlia;

/// <summary>
/// Proxy for AnimatorComponent playback control on an entity.
/// Call <see cref="Entity.GetAnimator"/> to obtain an instance.
/// </summary>
public sealed class AnimatorProxy
{
    private readonly ulong _id;
    internal AnimatorProxy(ulong id) { _id = id; }

    /// <summary>True while the animator is advancing; false when paused or stopped.</summary>
    public bool  IsPlaying { get => NativeApi.SA_Animator_IsPlaying(_id) != 0;
                             set => NativeApi.SA_Animator_SetPlaying(_id, value ? 1 : 0); }
    /// <summary>Playback speed multiplier (1 = normal, 0 = paused, negative = reverse).</summary>
    public float Speed     { get => NativeApi.SA_Animator_GetSpeed(_id);
                             set => NativeApi.SA_Animator_SetSpeed(_id, value); }

    /// <summary>Resumes playback (sets <see cref="IsPlaying"/> to true).</summary>
    public void Play()  => IsPlaying = true;
    /// <summary>Pauses playback (sets <see cref="IsPlaying"/> to false).</summary>
    public void Stop()  => IsPlaying = false;
}
