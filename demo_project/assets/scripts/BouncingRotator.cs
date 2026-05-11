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
        originY = Self.GetPosition().Y;
        rotY = Self.GetRotationEuler().Y;
    }

    public override void OnUpdate(float dt)
    {        
	Debug.Log("UPDATE");
        float t = Time.TotalTime;

        var pos = Self.GetPosition();
        pos.Y = originY + MathF.Sin(t * bobFrequency * MathF.PI * 2f) * bobAmplitude;
        Self.SetPosition(pos);

        rotY += rotateSpeed * dt;
        Self.SetRotationEuler(new Vector3(0f, rotY, 0f));
    }
}
