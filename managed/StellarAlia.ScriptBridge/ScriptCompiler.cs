using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using System.Reflection;

namespace StellarAlia.Bridge;

internal sealed class ScriptCompiler
{
    internal ScriptCompiler() { }

    internal byte[] Compile(string[] sourcePaths) {
        var sources = sourcePaths.Select(p =>
            CSharpSyntaxTree.ParseText(
                File.ReadAllText(p),
                path: p,
                encoding: System.Text.Encoding.UTF8));

        var options = new CSharpCompilationOptions(
            OutputKind.DynamicallyLinkedLibrary,
            optimizationLevel: OptimizationLevel.Debug,
            nullableContextOptions: NullableContextOptions.Enable);

        var compilation = CSharpCompilation.Create(
            "UserScripts",
            syntaxTrees: sources,
            references: BuildReferences(),
            options: options);

        using var ms = new MemoryStream();
        var result = compilation.Emit(ms);

        if (!result.Success) {
            var errors = result.Diagnostics
                .Where(d => d.Severity == DiagnosticSeverity.Error)
                .Select(d => d.ToString());
            throw new ScriptCompileException(string.Join('\n', errors));
        }

        return ms.ToArray();
    }

    // Use SDK reference assemblies (pure API surface, no native DLLs) — same strategy as Unity/Godot.
    // Runtime path:   <dotnet>/shared/Microsoft.NETCore.App/<ver>/
    // Reference path: <dotnet>/packs/Microsoft.NETCore.App.Ref/<ver>/ref/net8.0/
    private static List<MetadataReference> BuildReferences() {
        string runtimeDir = Path.GetDirectoryName(typeof(object).Assembly.Location)!;
        string? refDir    = FindRefAssemblyDir();

        // Key = simple assembly name. Ref pack entries are added first and take precedence.
        var byName = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        if (refDir != null) {
            foreach (var dll in Directory.EnumerateFiles(refDir, "*.dll"))
                byName[Path.GetFileNameWithoutExtension(dll)] = dll;
        } else {
            // Fallback: runtime dir, skip native DLLs
            foreach (var dll in Directory.EnumerateFiles(runtimeDir, "*.dll")) {
                try {
                    string name = AssemblyName.GetAssemblyName(dll).Name!;
                    byName[name] = dll;
                } catch { }
            }
        }

        // Add engine / project assemblies (e.g. StellarAlia.Runtime) from the loaded AppDomain.
        // When using the ref pack, skip any assembly that lives in the dotnet runtime dir —
        // those are implementation assemblies whose API surface is already covered by the ref pack.
        // Mixing them causes CS0433 (e.g. Vector3 in both System.Private.CoreLib and System.Runtime).
        foreach (var asm in AppDomain.CurrentDomain.GetAssemblies()) {
            if (asm.IsDynamic || string.IsNullOrEmpty(asm.Location)) continue;
            if (refDir != null &&
                asm.Location.StartsWith(runtimeDir, StringComparison.OrdinalIgnoreCase)) continue;
            string name = asm.GetName().Name!;
            if (!byName.ContainsKey(name))
                byName[name] = asm.Location;
        }

        return byName.Values
            .Select(p => (MetadataReference)MetadataReference.CreateFromFile(p))
            .ToList();
    }

    private static string? FindRefAssemblyDir() {
        // typeof(object) lives in:  <dotnet>/shared/Microsoft.NETCore.App/<version>/
        // packs live in:            <dotnet>/packs/
        // → must go up 3 levels from runtimeDir to reach the dotnet root
        string runtimeDir  = Path.GetDirectoryName(typeof(object).Assembly.Location)!;
        string version     = Path.GetFileName(runtimeDir);
        string? netCoreDir = Path.GetDirectoryName(runtimeDir);           // .../Microsoft.NETCore.App
        string? sharedDir  = Path.GetDirectoryName(netCoreDir!);          // .../shared
        string? dotnetRoot = Path.GetDirectoryName(sharedDir!);           // .../dotnet  ← correct root
        if (dotnetRoot == null) return null;

        // Exact version match
        string exact = Path.Combine(dotnetRoot, "packs", "Microsoft.NETCore.App.Ref", version, "ref", "net8.0");
        if (Directory.Exists(exact)) return exact;

        // Any 8.x ref pack, newest first
        string packsDir = Path.Combine(dotnetRoot, "packs", "Microsoft.NETCore.App.Ref");
        if (!Directory.Exists(packsDir)) return null;

        return Directory.GetDirectories(packsDir)
            .Where(d => Path.GetFileName(d).StartsWith("8."))
            .OrderByDescending(d => d)
            .Select(d => Path.Combine(d, "ref", "net8.0"))
            .FirstOrDefault(Directory.Exists);
    }
}

internal sealed class ScriptCompileException : Exception
{
    internal ScriptCompileException(string diagnostics) : base(diagnostics) { }
}
