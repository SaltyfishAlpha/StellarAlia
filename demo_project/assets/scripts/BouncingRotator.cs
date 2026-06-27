using System;
using System.Numerics;
using StellarAlia;

/// Bounces the entity up and down while rotating it on the Y axis.
/// Attach to any mesh entity to verify the scripting pipeline.
public class BouncingRotator : ScriptBase
{
    float rotateSpeed  = 90f;   // degrees per second
    float bobAmplitude = 0.8f;  // units
    float bobFrequency = 1.2f;  // cycles per second

    float originY;
    float rotY;

    public override void OnStart()
    {
        Debug.Log("START");
        originY = Self.LocalPosition.Y;
        rotY = Self.LocalRotationEuler.Y;
    }

    public override void OnUpdate(float dt)
    {
        float t = Time.TotalTime;

        var pos = Self.LocalPosition;
        pos.Y = originY + MathF.Sin(t * bobFrequency * MathF.PI * 2f) * bobAmplitude;
        Self.LocalPosition = pos;

        rotY += rotateSpeed * dt;
        Self.LocalRotationEuler = new Vector3(0f, rotY, 0f);
    }
}
