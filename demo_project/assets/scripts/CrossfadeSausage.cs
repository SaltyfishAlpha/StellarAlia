using StellarAlia;

// #83 P2-5 smoke test — drives the new AnimatorProxy clip-control API on a timer
// so no input is needed: just enter Play and watch. Every `interval` seconds it
// switches clip, alternating CrossfadeTo (fade) with one SetClip (hard cut) so
// both paths are exercised.
//
// Watch the console log for AnimationSystem's report of each switch:
//   "AnimationSystem: crossfade -> 'Skeleton|Spin'"   (from CrossfadeTo)
//   "AnimationSystem: swap -> 'Skeleton|Wiggle'"      (from SetClip / hard cut)
public class CrossfadeSausage : ScriptBase
{
    // sausage .saanim UUIDs (Base / Spin / Wiggle)
    const string Base   = "7908bc26-128f-4331-bee1-63b916d629e3";
    const string Spin   = "cdaed554-1a42-40f7-8d64-21607fb0365c";
    const string Wiggle = "504cee82-0205-41b5-9beb-e72b44923cc9";

    float fade     = 0.4f;  // exaggerated so the blend is easy to see
    float interval = 2.0f;  // seconds between switches

    float timer;
    int   step;
    AnimatorProxy? anim;

    public override void OnStart()
    {
        anim = Self.GetAnimator();
        Debug.Log($"[CrossfadeSausage] OnStart, animator present = {anim != null}");
    }

    public override void OnUpdate(float dt)
    {
        if (anim == null) return;
        timer += dt;
        if (timer < interval) return;
        timer = 0f;

        step = (step + 1) % 4;
        switch (step)
        {
            case 0: anim.CrossfadeTo(Base, fade); Debug.Log("[CrossfadeSausage] CrossfadeTo Base");            break;
            case 1: anim.CrossfadeTo(Spin, fade); Debug.Log("[CrossfadeSausage] CrossfadeTo Spin");            break;
            case 2: anim.SetClip(Wiggle);         Debug.Log("[CrossfadeSausage] SetClip Wiggle (hard cut)");   break;
            case 3: anim.CrossfadeTo(Spin, fade); Debug.Log("[CrossfadeSausage] CrossfadeTo Spin");            break;
        }
    }
}
