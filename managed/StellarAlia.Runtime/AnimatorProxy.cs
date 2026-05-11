namespace StellarAlia;

public sealed class AnimatorProxy
{
    private readonly ulong _id;
    internal AnimatorProxy(ulong id) { _id = id; }

    public bool  IsPlaying { get => NativeApi.SA_Animator_IsPlaying(_id) != 0;
                             set => NativeApi.SA_Animator_SetPlaying(_id, value ? 1 : 0); }
    public float Speed     { get => NativeApi.SA_Animator_GetSpeed(_id);
                             set => NativeApi.SA_Animator_SetSpeed(_id, value); }

    public void Play()  => IsPlaying = true;
    public void Stop()  => IsPlaying = false;
}
