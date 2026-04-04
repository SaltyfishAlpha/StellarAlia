#include "ImportScanner.hpp"
#include "TextureCook.hpp"
#include "MeshCook.hpp"
#include "MaterialCook.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia::Cook;

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

    for (const auto& inputDir : opts.inputDirs) {
        std::cout << "\n[Scan] " << inputDir << "\n";

        std::vector<AssetEntry> assets = ScanAndImport(inputDir);
        totalAssets += static_cast<int>(assets.size());

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
