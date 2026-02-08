#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <algorithm>

namespace wowee {
namespace pipeline {

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

    initialized = true;
    LOG_INFO("Asset manager initialized successfully");
    return true;
}

void AssetManager::shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down asset manager");

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

    // Read BLP file from MPQ (must hold readMutex — StormLib is not thread-safe)
    std::vector<uint8_t> blpData;
    {
        std::lock_guard<std::mutex> lock(readMutex);
        blpData = mpqManager.readFile(normalizedPath);
    }
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

    LOG_INFO("Loaded texture: ", normalizedPath, " (", image.width, "x", image.height, ")");
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

    std::lock_guard<std::mutex> lock(readMutex);
    return mpqManager.readFile(normalizePath(path));
}

void AssetManager::clearCache() {
    dbcCache.clear();
    LOG_INFO("Cleared asset cache");
}

std::string AssetManager::normalizePath(const std::string& path) const {
    std::string normalized = path;

    // Convert forward slashes to backslashes (WoW uses backslashes)
    std::replace(normalized.begin(), normalized.end(), '/', '\\');

    return normalized;
}

} // namespace pipeline
} // namespace wowee
