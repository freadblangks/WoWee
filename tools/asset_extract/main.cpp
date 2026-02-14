#include "extractor.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <cstring>

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " --mpq-dir <path> --output <path> [options]\n"
              << "\n"
              << "Extract WoW MPQ archives to organized loose files with manifest.\n"
              << "\n"
              << "Required:\n"
              << "  --mpq-dir <path>    Path to WoW Data directory containing MPQ files\n"
              << "  --output <path>     Output directory for extracted assets\n"
              << "\n"
              << "Options:\n"
              << "  --expansion <id>    Expansion: classic, turtle, tbc, wotlk (default: auto-detect)\n"
              << "  --locale <id>       Locale: enUS, deDE, frFR, etc. (default: auto-detect)\n"
              << "  --only-used-dbcs    Extract only the DBCs wowee uses (no other assets)\n"
              << "  --skip-dbc          Do not extract DBFilesClient/*.dbc (visual assets only)\n"
              << "  --dbc-csv           Convert selected DBFilesClient/*.dbc to CSV under\n"
              << "                      <output>/expansions/<expansion>/db/*.csv (for committing)\n"
              << "  --as-overlay <id>   Extract as expansion overlay (only files differing from base\n"
              << "                      manifest at <output>/manifest.json). Stores overlay assets in\n"
              << "                      <output>/expansions/<id>/overlay/ and implies --dbc-csv.\n"
              << "                      Auto-detected when base manifest already exists.\n"
              << "  --full-base         Force full base extraction even if manifest exists\n"
              << "  --reference-manifest <path>\n"
              << "                      Only extract files NOT in this manifest (delta extraction)\n"
              << "  --dbc-csv-out <dir> Write CSV DBCs into <dir> (overrides default output path)\n"
              << "  --verify            CRC32 verify all extracted files\n"
              << "  --threads <N>       Number of extraction threads (default: auto)\n"
              << "  --verbose           Verbose output\n"
              << "  --help              Show this help\n";
}

int main(int argc, char** argv) {
    wowee::tools::Extractor::Options opts;
    std::string expansion;
    std::string locale;
    bool forceBase = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mpq-dir") == 0 && i + 1 < argc) {
            opts.mpqDir = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            opts.outputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--expansion") == 0 && i + 1 < argc) {
            expansion = argv[++i];
        } else if (std::strcmp(argv[i], "--locale") == 0 && i + 1 < argc) {
            locale = argv[++i];
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            opts.threads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--only-used-dbcs") == 0) {
            opts.onlyUsedDbcs = true;
        } else if (std::strcmp(argv[i], "--skip-dbc") == 0) {
            opts.skipDbcExtraction = true;
        } else if (std::strcmp(argv[i], "--dbc-csv") == 0) {
            opts.generateDbcCsv = true;
        } else if (std::strcmp(argv[i], "--dbc-csv-out") == 0 && i + 1 < argc) {
            opts.dbcCsvOutputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--as-overlay") == 0 && i + 1 < argc) {
            opts.asOverlay = argv[++i];
            opts.generateDbcCsv = true; // Overlay mode always generates per-expansion CSVs
        } else if (std::strcmp(argv[i], "--full-base") == 0) {
            forceBase = true;
        } else if (std::strcmp(argv[i], "--reference-manifest") == 0 && i + 1 < argc) {
            opts.referenceManifest = argv[++i];
        } else if (std::strcmp(argv[i], "--verify") == 0) {
            opts.verify = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (opts.mpqDir.empty() || opts.outputDir.empty()) {
        std::cerr << "Error: --mpq-dir and --output are required\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Auto-detect expansion if not specified
    if (expansion.empty() || expansion == "auto") {
        expansion = wowee::tools::Extractor::detectExpansion(opts.mpqDir);
        if (expansion.empty()) {
            std::cerr << "Error: Could not auto-detect expansion. No known MPQ archives found in: "
                      << opts.mpqDir << "\n"
                      << "Specify manually with --expansion classic|tbc|wotlk\n";
            return 1;
        }
        std::cout << "Auto-detected expansion: " << expansion << "\n";
    }
    opts.expansion = expansion;

    // Auto-detect locale if not specified
    if (locale.empty() || locale == "auto") {
        locale = wowee::tools::Extractor::detectLocale(opts.mpqDir);
        if (locale.empty()) {
            std::cerr << "Warning: No locale directory found, skipping locale-specific archives\n";
        } else {
            std::cout << "Auto-detected locale: " << locale << "\n";
        }
    }
    opts.locale = locale;

    // Auto-detect overlay mode: if a base manifest already exists and this expansion
    // has a profile directory, automatically use overlay mode so the user doesn't have
    // to think about extraction order.
    if (opts.asOverlay.empty() && !forceBase && !opts.onlyUsedDbcs) {
        namespace fs = std::filesystem;
        std::string baseManifest = opts.outputDir + "/manifest.json";
        std::string expJson = opts.outputDir + "/expansions/" + expansion + "/expansion.json";
        if (fs::exists(baseManifest) && fs::exists(expJson)) {
            opts.asOverlay = expansion;
            opts.generateDbcCsv = true;
            std::cout << "Base manifest found — auto-overlay mode for " << expansion << "\n";
        }
    }

    std::cout << "=== Wowee Asset Extractor ===\n";
    std::cout << "MPQ directory: " << opts.mpqDir << "\n";
    std::cout << "Output:        " << opts.outputDir << "\n";
    std::cout << "Expansion:     " << expansion << "\n";
    if (!locale.empty()) {
        std::cout << "Locale:        " << locale << "\n";
    }
    if (opts.onlyUsedDbcs) {
        std::cout << "Mode:          only-used-dbcs\n";
    }
    if (opts.skipDbcExtraction) {
        std::cout << "DBC extract:   skipped\n";
    }
    if (opts.generateDbcCsv) {
        std::cout << "DBC CSV:       enabled\n";
        if (!opts.dbcCsvOutputDir.empty()) {
            std::cout << "DBC CSV out:   " << opts.dbcCsvOutputDir << "\n";
        }
    }

    if (!opts.asOverlay.empty()) {
        std::cout << "Overlay:       " << opts.asOverlay << " (only files differing from base)\n";
    }
    if (!opts.referenceManifest.empty()) {
        std::cout << "Reference:     " << opts.referenceManifest << " (delta mode)\n";
    }

    if (!wowee::tools::Extractor::run(opts)) {
        std::cerr << "Extraction failed!\n";
        return 1;
    }

    return 0;
}
