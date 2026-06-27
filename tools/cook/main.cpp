#include "importer/ImportScanner.hpp"
#include "importer/TextureImporter.hpp"
#include "importer/MeshImporter.hpp"
#include "importer/MaterialImporter.hpp"
#include "importer/InputMapImporter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia::Import;
using StellarAlia::AssetID;

// ─── CLI argument parsing ─────────────────────────────────────────────────────

struct Options {
    std::vector<fs::path> inputDirs;
    fs::path              outputDir;
    bool                  force      = false;
    bool                  importOnly = false;
};

static void PrintUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --input  <dir>   Asset directory to scan and cook (repeatable)\n"
        << "  --output <dir>   Cook cache output directory\n"
        << "  --force          Re-cook all assets even if up-to-date\n"
        << "  --import-only    Generate .sameta files but do not cook\n"
        << "  --help           Show this message\n"
        << "\n"
        << "Example:\n"
        << "  StellarAliaCook --input assets/ --output build/cook_cache/\n";
}

static Options ParseArgs(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else if (arg == "--force") {
            opts.force = true;
        } else if (arg == "--import-only") {
            opts.importOnly = true;
        } else if (arg == "--input" && i + 1 < argc) {
            opts.inputDirs.emplace_back(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            opts.outputDir = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }
    return opts;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Options opts = ParseArgs(argc, argv);

    if (opts.inputDirs.empty()) {
        std::cerr << "Error: at least one --input directory is required.\n";
        PrintUsage(argv[0]);
        return 1;
    }
    if (opts.outputDir.empty() && !opts.importOnly) {
        std::cerr << "Error: --output directory is required (or use --import-only).\n";
        PrintUsage(argv[0]);
        return 1;
    }

    int totalAssets  = 0;
    int cookedOk     = 0;
    int cookedFailed = 0;
    int skipped      = 0;

    // UUID (hi^lo key) → absolute source path for every Mesh asset encountered.
    // Used in the second pass to resolve .sanim source_mesh references.
    std::unordered_map<uint64_t, fs::path> meshPathByUUID;

    for (const auto& inputDir : opts.inputDirs) {
        std::cout << "\n[Scan] " << inputDir << "\n";

        std::vector<AssetEntry> assets = ScanAndImport(inputDir);
        totalAssets += static_cast<int>(assets.size());

        // Build UUID→path index for Mesh assets regardless of importOnly.
        for (const auto& e : assets)
            if (e.meta.type == "Mesh")
                meshPathByUUID[e.meta.uuid.hi ^ e.meta.uuid.lo] = e.sourcePath;

        if (opts.importOnly) continue;

        for (const auto& entry : assets) {
            bool ok = false;
            if (entry.meta.type == "Texture") {
                ok = CookTexture(entry, opts.outputDir, opts.force);
            } else if (entry.meta.type == "Mesh") {
                ok = CookMesh(entry, opts.outputDir, opts.force);
            } else if (entry.meta.type == "Material") {
                ok = CookStandaloneMaterial(entry.sourcePath, entry.meta.uuid,
                                            opts.outputDir, opts.force);
            } else if (entry.meta.type == "InputMap") {
                ok = CookInputMap(entry, opts.outputDir, opts.force);
            } else {
                std::cout << "[Cook] SKIP (unsupported type: "
                          << entry.meta.type << ")  "
                          << entry.sourcePath.filename() << '\n';
                ++skipped;
                continue;
            }

            if (ok) ++cookedOk;
            else    ++cookedFailed;
        }
    }

    // ── Second pass: cook standalone .sanim sidecars ──────────────────────────
    // .sanim files are not returned by ScanAndImport (they are sidecars, not
    // primary assets), so we scan explicitly for *.sanim.sameta files.
    if (!opts.importOnly) {
        for (const auto& inputDir : opts.inputDirs) {
            std::error_code ec;
            for (const auto& de : fs::recursive_directory_iterator(
                     inputDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (!de.is_regular_file(ec)) continue;
                const fs::path& p = de.path();
                // Match files ending in ".sanim.sameta"
                if (p.extension() != ".sameta") continue;
                const fs::path sanimPath = p.parent_path() / p.stem(); // strip .sameta
                if (sanimPath.extension() != ".sanim") continue;
                if (!fs::exists(sanimPath)) continue;

                // Load the .sanim.sameta to get the UUID.
                MetaFile meta;
                if (!MetaFile::Load(p, meta) || meta.type != "Animation") continue;

                // Parse source_mesh UUID from the .sanim file.
                AssetID sourceMeshUUID;
                {
                    std::ifstream f(sanimPath);
                    std::string line;
                    while (std::getline(f, line)) {
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                            line.pop_back();
                        if (line.empty() || line[0] == '#') continue;
                        const auto eq = line.find('=');
                        if (eq == std::string::npos) continue;
                        if (line.substr(0, eq) == "source_mesh")
                            sourceMeshUUID = AssetID::FromString(line.substr(eq + 1));
                    }
                }
                if (!sourceMeshUUID.IsValid()) continue;

                const auto it = meshPathByUUID.find(sourceMeshUUID.hi ^ sourceMeshUUID.lo);
                if (it == meshPathByUUID.end()) {
                    std::cerr << "[Cook] WARN  " << sanimPath.filename()
                              << " — source mesh UUID not found in scanned dirs\n";
                    continue;
                }

                AssetEntry sanimEntry;
                sanimEntry.sourcePath = sanimPath;
                sanimEntry.metaPath   = p;
                sanimEntry.meta       = meta;

                const bool ok = CookAnimSidecar(sanimEntry, it->second,
                                                opts.outputDir, opts.force);
                if (ok) ++cookedOk;
                else    ++cookedFailed;
            }
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "Assets found : " << totalAssets  << '\n';
    if (!opts.importOnly) {
        std::cout << "Cooked OK    : " << cookedOk     << '\n';
        std::cout << "Failed       : " << cookedFailed << '\n';
        std::cout << "Skipped      : " << skipped      << '\n';
    }

    return cookedFailed > 0 ? 1 : 0;
}
