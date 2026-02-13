#include "extractor.hpp"
#include "path_mapper.hpp"
#include "manifest_writer.hpp"

#include <StormLib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#endif

namespace wowee {
namespace tools {

namespace fs = std::filesystem;

// Archive descriptor for priority-based loading
struct ArchiveDesc {
    std::string path;
    int priority;
};

static std::string toLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string normalizeWowPath(const std::string& path) {
    std::string n = path;
    std::replace(n.begin(), n.end(), '/', '\\');
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return n;
}

// Discover archive files in the same priority order as MPQManager
static std::vector<ArchiveDesc> discoverArchives(const std::string& mpqDir) {
    std::vector<ArchiveDesc> result;

    auto tryAdd = [&](const std::string& name, int prio) {
        std::string fullPath = mpqDir + "/" + name;
        if (fs::exists(fullPath)) {
            result.push_back({fullPath, prio});
        }
    };

    // Base archives (priority 100)
    tryAdd("common.MPQ", 100);
    tryAdd("common-2.MPQ", 100);
    tryAdd("expansion.MPQ", 100);
    tryAdd("lichking.MPQ", 100);

    // Patch archives (priority 150-500)
    tryAdd("patch.MPQ", 150);
    tryAdd("patch-2.MPQ", 200);
    tryAdd("patch-3.MPQ", 300);
    tryAdd("patch-4.MPQ", 400);
    tryAdd("patch-5.MPQ", 500);

    // Letter patches (priority 800-925)
    for (char c = 'a'; c <= 'z'; ++c) {
        std::string lower = std::string("patch-") + c + ".mpq";
        std::string upper = std::string("Patch-") + static_cast<char>(std::toupper(c)) + ".mpq";
        int prioLower = 800 + (c - 'a');
        int prioUpper = 900 + (c - 'a');
        tryAdd(lower, prioLower);
        tryAdd(upper, prioUpper);
    }

    // Locale archives
    tryAdd("enUS/backup-enUS.MPQ", 230);
    tryAdd("enUS/base-enUS.MPQ", 235);
    tryAdd("enUS/speech-enUS.MPQ", 240);
    tryAdd("enUS/expansion-speech-enUS.MPQ", 245);
    tryAdd("enUS/expansion-locale-enUS.MPQ", 246);
    tryAdd("enUS/lichking-speech-enUS.MPQ", 248);
    tryAdd("enUS/lichking-locale-enUS.MPQ", 249);
    tryAdd("enUS/locale-enUS.MPQ", 250);
    tryAdd("enUS/patch-enUS.MPQ", 450);
    tryAdd("enUS/patch-enUS-2.MPQ", 460);
    tryAdd("enUS/patch-enUS-3.MPQ", 470);

    // Sort by priority so highest-priority archives are last
    // (we'll iterate highest-prio first when extracting)
    std::sort(result.begin(), result.end(),
              [](const ArchiveDesc& a, const ArchiveDesc& b) { return a.priority < b.priority; });

    return result;
}

bool Extractor::enumerateFiles(const Options& opts,
                               std::vector<std::string>& outFiles) {
    // Open all archives, enumerate files from highest priority to lowest.
    // Use a set to deduplicate (highest-priority version wins).
    auto archives = discoverArchives(opts.mpqDir);
    if (archives.empty()) {
        std::cerr << "No MPQ archives found in: " << opts.mpqDir << "\n";
        return false;
    }

    std::cout << "Found " << archives.size() << " MPQ archives\n";

    // Enumerate from highest priority first so first-seen files win
    std::set<std::string> seenNormalized;
    std::vector<std::pair<std::string, std::string>> fileList; // (original name, archive path)

    for (auto it = archives.rbegin(); it != archives.rend(); ++it) {
        HANDLE hMpq = nullptr;
        if (!SFileOpenArchive(it->path.c_str(), 0, 0, &hMpq)) {
            std::cerr << "  Failed to open: " << it->path << "\n";
            continue;
        }

        if (opts.verbose) {
            std::cout << "  Scanning: " << it->path << " (priority " << it->priority << ")\n";
        }

        SFILE_FIND_DATA findData;
        HANDLE hFind = SFileFindFirstFile(hMpq, "*", &findData, nullptr);
        if (hFind) {
            do {
                std::string fileName = findData.cFileName;
                // Skip internal listfile/attributes
                if (fileName == "(listfile)" || fileName == "(attributes)" ||
                    fileName == "(signature)" || fileName == "(patch_metadata)") {
                    continue;
                }

                std::string norm = normalizeWowPath(fileName);
                if (seenNormalized.insert(norm).second) {
                    // First time seeing this file — this is the highest-priority version
                    outFiles.push_back(fileName);
                }
            } while (SFileFindNextFile(hFind, &findData));
            SFileFindClose(hFind);
        }

        SFileCloseArchive(hMpq);
    }

    std::cout << "Enumerated " << outFiles.size() << " unique files\n";
    return true;
}

bool Extractor::run(const Options& opts) {
    auto startTime = std::chrono::steady_clock::now();

    // Enumerate all unique files across all archives
    std::vector<std::string> files;
    if (!enumerateFiles(opts, files)) {
        return false;
    }

    if (files.empty()) {
        std::cerr << "No files to extract\n";
        return false;
    }

    // Create output directory
    std::error_code ec;
    fs::create_directories(opts.outputDir, ec);
    if (ec) {
        std::cerr << "Failed to create output directory: " << ec.message() << "\n";
        return false;
    }

    auto archives = discoverArchives(opts.mpqDir);

    // Determine thread count
    int numThreads = opts.threads;
    if (numThreads <= 0) {
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
        if (numThreads <= 0) numThreads = 4;
    }

    Stats stats;
    std::mutex manifestMutex;
    std::vector<ManifestWriter::FileEntry> manifestEntries;

    // Partition files across threads
    std::atomic<size_t> fileIndex{0};
    size_t totalFiles = files.size();

    auto workerFn = [&]() {
        // Each thread opens ALL archives independently (StormLib is not thread-safe per handle).
        // Sorted highest-priority last, so we iterate in reverse to find the winning version.
        struct ThreadArchive {
            HANDLE handle;
            int priority;
        };
        std::vector<ThreadArchive> threadHandles;
        for (const auto& ad : archives) {
            HANDLE h = nullptr;
            if (SFileOpenArchive(ad.path.c_str(), 0, 0, &h)) {
                threadHandles.push_back({h, ad.priority});
            }
        }
        if (threadHandles.empty()) {
            std::cerr << "Worker thread: failed to open any archives\n";
            return;
        }

        while (true) {
            size_t idx = fileIndex.fetch_add(1);
            if (idx >= totalFiles) break;

            const std::string& wowPath = files[idx];
            std::string normalized = normalizeWowPath(wowPath);

            // Map to new filesystem path
            std::string mappedPath = PathMapper::mapPath(wowPath);
            std::string fullOutputPath = opts.outputDir + "/" + mappedPath;

            // Search archives in reverse priority order (highest priority first)
            HANDLE hFile = nullptr;
            for (auto it = threadHandles.rbegin(); it != threadHandles.rend(); ++it) {
                if (SFileOpenFileEx(it->handle, wowPath.c_str(), 0, &hFile)) {
                    break;
                }
                hFile = nullptr;
            }
            if (!hFile) {
                stats.filesFailed++;
                continue;
            }

            DWORD fileSize = SFileGetFileSize(hFile, nullptr);
            if (fileSize == SFILE_INVALID_SIZE || fileSize == 0) {
                SFileCloseFile(hFile);
                stats.filesSkipped++;
                continue;
            }

            std::vector<uint8_t> data(fileSize);
            DWORD bytesRead = 0;
            if (!SFileReadFile(hFile, data.data(), fileSize, &bytesRead, nullptr)) {
                SFileCloseFile(hFile);
                stats.filesFailed++;
                continue;
            }
            SFileCloseFile(hFile);
            data.resize(bytesRead);

            // Create output directory
            fs::path outPath(fullOutputPath);
            fs::create_directories(outPath.parent_path(), ec);

            // Write file
            std::ofstream out(fullOutputPath, std::ios::binary);
            if (!out.is_open()) {
                stats.filesFailed++;
                continue;
            }
            out.write(reinterpret_cast<const char*>(data.data()), data.size());
            out.close();

            // Compute CRC32
            uint32_t crc = ManifestWriter::computeCRC32(data.data(), data.size());

            // Add manifest entry
            ManifestWriter::FileEntry entry;
            entry.wowPath = normalized;
            entry.filesystemPath = mappedPath;
            entry.size = data.size();
            entry.crc32 = crc;

            {
                std::lock_guard<std::mutex> lock(manifestMutex);
                manifestEntries.push_back(std::move(entry));
            }

            stats.filesExtracted++;
            stats.bytesExtracted += data.size();

            // Progress
            uint64_t done = stats.filesExtracted.load();
            if (done % 1000 == 0) {
                std::cout << "\r  Extracted " << done << " / " << totalFiles << " files..."
                          << std::flush;
            }
        }

        for (auto& th : threadHandles) {
            SFileCloseArchive(th.handle);
        }
    };

    std::cout << "Extracting " << totalFiles << " files using " << numThreads << " threads...\n";

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(workerFn);
    }
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "\r  Extracted " << stats.filesExtracted.load() << " files ("
              << stats.bytesExtracted.load() / (1024 * 1024) << " MB), "
              << stats.filesSkipped.load() << " skipped, "
              << stats.filesFailed.load() << " failed\n";

    // Sort manifest entries for deterministic output
    std::sort(manifestEntries.begin(), manifestEntries.end(),
              [](const ManifestWriter::FileEntry& a, const ManifestWriter::FileEntry& b) {
                  return a.wowPath < b.wowPath;
              });

    // Write manifest
    std::string manifestPath = opts.outputDir + "/manifest.json";
    // basePath is "." since manifest sits inside the output directory
    if (!ManifestWriter::write(manifestPath, ".", manifestEntries)) {
        std::cerr << "Failed to write manifest: " << manifestPath << "\n";
        return false;
    }

    std::cout << "Wrote manifest: " << manifestPath << " (" << manifestEntries.size() << " entries)\n";

    // Verification pass
    if (opts.verify) {
        std::cout << "Verifying extracted files...\n";
        uint64_t verified = 0, verifyFailed = 0;
        for (const auto& entry : manifestEntries) {
            std::string fsPath = opts.outputDir + "/" + entry.filesystemPath;
            std::ifstream f(fsPath, std::ios::binary | std::ios::ate);
            if (!f.is_open()) {
                std::cerr << "  MISSING: " << fsPath << "\n";
                verifyFailed++;
                continue;
            }

            auto size = f.tellg();
            if (static_cast<uint64_t>(size) != entry.size) {
                std::cerr << "  SIZE MISMATCH: " << fsPath << " (expected "
                          << entry.size << ", got " << size << ")\n";
                verifyFailed++;
                continue;
            }

            f.seekg(0);
            std::vector<uint8_t> data(static_cast<size_t>(size));
            f.read(reinterpret_cast<char*>(data.data()), size);

            uint32_t crc = ManifestWriter::computeCRC32(data.data(), data.size());
            if (crc != entry.crc32) {
                std::cerr << "  CRC MISMATCH: " << fsPath << "\n";
                verifyFailed++;
                continue;
            }

            verified++;
        }
        std::cout << "Verified " << verified << " files";
        if (verifyFailed > 0) {
            std::cout << " (" << verifyFailed << " FAILED)";
        }
        std::cout << "\n";
    }

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    std::cout << "Done in " << secs / 60 << "m " << secs % 60 << "s\n";

    return true;
}

} // namespace tools
} // namespace wowee
