#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace wowee {
namespace tools {

/**
 * Extraction pipeline: MPQ archives → loose files + manifest
 */
class Extractor {
public:
    struct Options {
        std::string mpqDir;       // Path to WoW Data directory
        std::string outputDir;    // Output directory for extracted assets
        int threads = 0;          // 0 = auto-detect
        bool verify = false;      // CRC32 verify after extraction
        bool verbose = false;     // Verbose logging
    };

    struct Stats {
        std::atomic<uint64_t> filesExtracted{0};
        std::atomic<uint64_t> bytesExtracted{0};
        std::atomic<uint64_t> filesSkipped{0};
        std::atomic<uint64_t> filesFailed{0};
    };

    /**
     * Run the extraction pipeline
     * @return true on success
     */
    static bool run(const Options& opts);

private:
    static bool enumerateFiles(const Options& opts,
                               std::vector<std::string>& outFiles);
};

} // namespace tools
} // namespace wowee
