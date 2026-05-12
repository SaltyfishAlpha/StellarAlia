using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;

namespace StellarAlia.Bridge;

internal enum LifecycleMethod : int
{
    OnAttach      = 0,
    OnStart       = 1,
    OnFixedUpdate = 2,
    OnUpdate      = 3,
    OnLateUpdate  = 4,
    OnStop        = 5,
    OnDetach      = 6,
}

internal sealed class ScriptLoader
{
    private CollectibleALC?               _alc;
    private readonly Dictionary<ulong, ScriptBase> _instances = new();
    private bool _updateBatchActive = false;
    private static readonly FieldInfo     s_entityIdField =
        typeof(ScriptBase).GetField("EntityId", BindingFlags.Instance | BindingFlags.NonPublic)!;

    internal void Load(byte[] assemblyBytes) {
        _alc = new CollectibleALC("UserScripts");
        using var ms = new MemoryStream(assemblyBytes);
        _alc.LoadFromStream(ms);
    }

    internal void Instantiate(ulong entityId, string className) {
        if (_alc is null) return;
        Type? type = null;
        foreach (var asm in _alc.Assemblies) {
            type = asm.GetTypes().FirstOrDefault(t =>
                t.Name == className && t.IsSubclassOf(typeof(ScriptBase)));
            if (type is not null) break;
        }
        if (type is null) return;

        var instance = (ScriptBase)Activator.CreateInstance(type)!;
        s_entityIdField.SetValue(instance, entityId);
        _instances[entityId] = instance;
    }

    internal void Invoke(ulong entityId, LifecycleMethod method, float arg) {
        if (!_instances.TryGetValue(entityId, out var inst)) return;
        try {
            switch (method) {
                case LifecycleMethod.OnAttach:      inst.OnAttach();          break;
                case LifecycleMethod.OnStart:       inst.OnStart();           break;
                case LifecycleMethod.OnFixedUpdate: inst.OnFixedUpdate(arg);  break;
                case LifecycleMethod.OnUpdate:
                    if (!_updateBatchActive) { Input.BeginFrame(); _updateBatchActive = true; }
                    inst.OnUpdate(arg);
                    break;
                case LifecycleMethod.OnLateUpdate:
                    _updateBatchActive = false;
                    inst.OnLateUpdate(arg);
                    break;
                case LifecycleMethod.OnStop:        inst.OnStop();            break;
                case LifecycleMethod.OnDetach:      inst.OnDetach();          break;
            }
        } catch (Exception ex) {
            StellarAlia.Log.Error($"Script exception on entity {entityId} [{method}]: {ex.Message}");
        }
    }

    internal void RemoveInstance(ulong entityId) {
        _instances.Remove(entityId);
    }

    internal void Unload() {
        _instances.Clear();
        _alc?.Unload();
        _alc = null;
    }

    private sealed class CollectibleALC : AssemblyLoadContext
    {
        internal CollectibleALC(string name) : base(name, isCollectible: true) { }

        // Delegate dependency resolution to already-loaded assemblies (across all ALCs).
        // AppDomain covers all contexts including the hostfxr-managed one that holds
        // StellarAlia.Runtime, whereas AssemblyLoadContext.Default does not.
        protected override Assembly? Load(AssemblyName assemblyName) =>
            AppDomain.CurrentDomain.GetAssemblies()
                .FirstOrDefault(a => a.GetName().Name == assemblyName.Name);
    }
}
