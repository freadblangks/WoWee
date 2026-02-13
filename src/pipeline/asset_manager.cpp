#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include "core/memory_monitor.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace wowee {
namespace pipeline {

namespace {
size_t parseEnvSizeMB(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return 0;
    }
    char* end = nullptr;
    unsigned long long mb = std::strtoull(v, &end, 10);
    if (end == v || mb == 0) {
        return 0;
    }
    if (mb > (std::numeric_limits<size_t>::max() / (1024ull * 1024ull))) {
        return 0;
    }
    return static_cast<size_t>(mb);
}
} // namespace

AssetManager::AssetManager() = default;
AssetManager::~AssetManager() {
    shutdown();
}

bool AssetManager::initialize(const std::string& dataPath_) {
    if (initialized) {
        LOG_WARNING("AssetManager already initialized");
        return true;
    }

    dataPath = dataPath_;
    LOG_INFO("Initializing asset manager with data path: ", dataPath);

    // Initialize MPQ manager
    if (!mpqManager.initialize(dataPath)) {
        LOG_ERROR("Failed to initialize MPQ manager");
        return false;
    }

    // Set dynamic file cache budget based on available RAM.
    // Bias toward MPQ decompressed-file caching to minimize runtime read/decompress stalls.
    auto& memMonitor = core::MemoryMonitor::getInstance();
    size_t recommendedBudget = memMonitor.getRecommendedCacheBudget();
    size_t dynamicBudget = (recommendedBudget * 3) / 4;  // 75% of global cache budget

    // The MemoryMonitor recommendation is intentionally aggressive; without a cap, large patch MPQs
    // can cause the decompressed-file cache to balloon into tens/hundreds of GB and push the OS into swap.
    // Provide env overrides and clamp to a safe default.
    const size_t envFixedMB = parseEnvSizeMB("WOWEE_FILE_CACHE_MB");
    const size_t envMaxMB = parseEnvSizeMB("WOWEE_FILE_CACHE_MAX_MB");

    const size_t minBudgetBytes = 256ull * 1024ull * 1024ull;          // 256 MB
    const size_t defaultMaxBudgetBytes = 2048ull * 1024ull * 1024ull;  // 2 GB
    const size_t maxBudgetBytes = (envMaxMB > 0)
        ? (envMaxMB * 1024ull * 1024ull)
        : defaultMaxBudgetBytes;

    if (envFixedMB > 0) {
        fileCacheBudget = envFixedMB * 1024ull * 1024ull;
        if (fileCacheBudget < minBudgetBytes) {
            fileCacheBudget = minBudgetBytes;
        }
        LOG_WARNING("Asset file cache fixed via WOWEE_FILE_CACHE_MB=", envFixedMB,
                    " (effective ", fileCacheBudget / (1024 * 1024), " MB)");
    } else {
        fileCacheBudget = std::clamp(dynamicBudget, minBudgetBytes, maxBudgetBytes);
    }

    initialized = true;
    LOG_INFO("Asset manager initialized (file cache: ",
             fileCacheBudget / (1024 * 1024), " MB, recommended=",
             recommendedBudget / (1024 * 1024), " MB, max=",
             maxBudgetBytes / (1024 * 1024), " MB)");
    return true;
}

void AssetManager::shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down asset manager");

    // Log cache statistics
    if (fileCacheHits + fileCacheMisses > 0) {
        float hitRate = (float)fileCacheHits / (fileCacheHits + fileCacheMisses) * 100.0f;
        LOG_INFO("File cache stats: ", fileCacheHits, " hits, ", fileCacheMisses, " misses (",
                 (int)hitRate, "% hit rate), ", fileCacheTotalBytes / 1024 / 1024, " MB cached");
    }

    clearCache();
    mpqManager.shutdown();

    initialized = false;
}

BLPImage AssetManager::loadTexture(const std::string& path) {
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return BLPImage();
    }

    // Normalize path
    std::string normalizedPath = normalizePath(path);

    LOG_DEBUG("Loading texture: ", normalizedPath);

    // Route through readFile() so decompressed MPQ bytes participate in file cache.
    std::vector<uint8_t> blpData = readFile(normalizedPath);
    if (blpData.empty()) {
        LOG_WARNING("Texture not found: ", normalizedPath);
        return BLPImage();
    }

    // Load BLP
    BLPImage image = BLPLoader::load(blpData);
    if (!image.isValid()) {
        LOG_ERROR("Failed to load texture: ", normalizedPath);
        return BLPImage();
    }

    LOG_DEBUG("Loaded texture: ", normalizedPath, " (", image.width, "x", image.height, ")");
    return image;
}

std::shared_ptr<DBCFile> AssetManager::loadDBC(const std::string& name) {
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return nullptr;
    }

    // Check cache first
    auto it = dbcCache.find(name);
    if (it != dbcCache.end()) {
        LOG_DEBUG("DBC already loaded (cached): ", name);
        return it->second;
    }

    LOG_DEBUG("Loading DBC: ", name);

    // Construct DBC path (DBFilesClient directory)
    std::string dbcPath = "DBFilesClient\\" + name;

    // Read DBC file from MPQ (must hold readMutex — StormLib is not thread-safe)
    std::vector<uint8_t> dbcData;
    {
        std::lock_guard<std::mutex> lock(readMutex);
        dbcData = mpqManager.readFile(dbcPath);
    }
    if (dbcData.empty()) {
        LOG_WARNING("DBC not found: ", dbcPath);
        return nullptr;
    }

    // Load DBC
    auto dbc = std::make_shared<DBCFile>();
    if (!dbc->load(dbcData)) {
        LOG_ERROR("Failed to load DBC: ", dbcPath);
        return nullptr;
    }

    // Cache the DBC
    dbcCache[name] = dbc;

    LOG_INFO("Loaded DBC: ", name, " (", dbc->getRecordCount(), " records)");
    return dbc;
}

std::shared_ptr<DBCFile> AssetManager::getDBC(const std::string& name) const {
    auto it = dbcCache.find(name);
    if (it != dbcCache.end()) {
        return it->second;
    }
    return nullptr;
}

bool AssetManager::fileExists(const std::string& path) const {
    if (!initialized) {
        return false;
    }

    std::lock_guard<std::mutex> lock(readMutex);
    return mpqManager.fileExists(normalizePath(path));
}

std::vector<uint8_t> AssetManager::readFile(const std::string& path) const {
    if (!initialized) {
        return std::vector<uint8_t>();
    }

    std::string normalized = normalizePath(path);
    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        // Check cache first
        auto it = fileCache.find(normalized);
        if (it != fileCache.end()) {
            // Cache hit - update access time and return cached data
            it->second.lastAccessTime = ++fileCacheAccessCounter;
            fileCacheHits++;
            return it->second.data;
        }
    }

    // Cache miss path: serialize MPQ reads. Before reading, re-check cache while holding
    // readMutex so only one thread performs decompression per hot path at a time.
    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> readLock(readMutex);
        {
            std::lock_guard<std::mutex> cacheLock(cacheMutex);
            auto it = fileCache.find(normalized);
            if (it != fileCache.end()) {
                it->second.lastAccessTime = ++fileCacheAccessCounter;
                fileCacheHits++;
                return it->second.data;
            }
            fileCacheMisses++;
        }
        data = mpqManager.readFile(normalized);
    }
    if (data.empty()) {
        return data;  // File not found
    }

    // Add to cache if within budget
    size_t fileSize = data.size();
    if (fileSize > 0 && fileSize < fileCacheBudget / 2) {  // Don't cache files > 50% of budget (very aggressive)
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        // Evict old entries if needed (LRU)
        while (fileCacheTotalBytes + fileSize > fileCacheBudget && !fileCache.empty()) {
            // Find least recently used entry
            auto lru = fileCache.begin();
            for (auto it = fileCache.begin(); it != fileCache.end(); ++it) {
                if (it->second.lastAccessTime < lru->second.lastAccessTime) {
                    lru = it;
                }
            }
            fileCacheTotalBytes -= lru->second.data.size();
            fileCache.erase(lru);
        }

        // Add new entry
        CachedFile cached;
        cached.data = data;
        cached.lastAccessTime = ++fileCacheAccessCounter;
        fileCache[normalized] = std::move(cached);
        fileCacheTotalBytes += fileSize;
    }

    return data;
}

std::vector<uint8_t> AssetManager::readFileOptional(const std::string& path) const {
    if (!initialized) {
        return std::vector<uint8_t>();
    }

    // Avoid MPQManager missing-file warnings for expected probe misses.
    if (!fileExists(path)) {
        return std::vector<uint8_t>();
    }
    return readFile(path);
}

void AssetManager::clearCache() {
    std::scoped_lock lock(readMutex, cacheMutex);
    dbcCache.clear();
    fileCache.clear();
    fileCacheTotalBytes = 0;
    fileCacheAccessCounter = 0;
    LOG_INFO("Cleared asset cache (DBC + file cache)");
}

std::string AssetManager::normalizePath(const std::string& path) const {
    std::string normalized = path;

    // Convert forward slashes to backslashes (WoW uses backslashes)
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    // Lowercase for case-insensitive cache keys (improves hit rate across mixed-case callers).
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return normalized;
}

} // namespace pipeline
} // namespace wowee
