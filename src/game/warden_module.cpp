#include "game/warden_module.hpp"
#include "auth/crypto.hpp"
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <zlib.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/sha.h>

namespace wowee {
namespace game {

// ============================================================================
// WardenModule Implementation
// ============================================================================

WardenModule::WardenModule()
    : loaded_(false)
    , moduleMemory_(nullptr)
    , moduleSize_(0)
{
}

WardenModule::~WardenModule() {
    unload();
}

bool WardenModule::load(const std::vector<uint8_t>& moduleData,
                        const std::vector<uint8_t>& md5Hash,
                        const std::vector<uint8_t>& rc4Key) {
    moduleData_ = moduleData;
    md5Hash_ = md5Hash;

    std::cout << "[WardenModule] Loading module (MD5: ";
    for (size_t i = 0; i < std::min(md5Hash.size(), size_t(8)); ++i) {
        printf("%02X", md5Hash[i]);
    }
    std::cout << "...)" << std::endl;

    // Step 1: Verify MD5 hash
    if (!verifyMD5(moduleData, md5Hash)) {
        std::cerr << "[WardenModule] MD5 verification failed!" << std::endl;
        return false;
    }
    std::cout << "[WardenModule] ✓ MD5 verified" << std::endl;

    // Step 2: RC4 decrypt
    if (!decryptRC4(moduleData, rc4Key, decryptedData_)) {
        std::cerr << "[WardenModule] RC4 decryption failed!" << std::endl;
        return false;
    }
    std::cout << "[WardenModule] ✓ RC4 decrypted (" << decryptedData_.size() << " bytes)" << std::endl;

    // Step 3: Verify RSA signature
    if (!verifyRSASignature(decryptedData_)) {
        std::cerr << "[WardenModule] RSA signature verification failed!" << std::endl;
        // Note: Currently returns true (skipping verification) due to placeholder modulus
    }

    // Step 4: zlib decompress
    if (!decompressZlib(decryptedData_, decompressedData_)) {
        std::cerr << "[WardenModule] zlib decompression failed!" << std::endl;
        return false;
    }

    // Step 5: Parse custom executable format
    // TODO: Parse skip/copy section structure
    // - Read alternating [2-byte length][data] sections
    // - Skip sections are ignored, copy sections loaded
    std::cout << "[WardenModule] ⏸ Executable format parsing (NOT IMPLEMENTED)" << std::endl;
    // if (!parseExecutableFormat(decompressedData_)) {
    //     return false;
    // }

    // Step 6: Apply relocations
    // TODO: Fix absolute address references
    // - Delta-encoded offsets with high-bit continuation
    // - Update all pointers relative to module base
    std::cout << "[WardenModule] ⏸ Address relocations (NOT IMPLEMENTED)" << std::endl;
    // if (!applyRelocations()) {
    //     return false;
    // }

    // Step 7: Bind APIs
    // TODO: Resolve kernel32.dll, user32.dll imports
    // - GetProcAddress for each required function
    // - Patch import table
    std::cout << "[WardenModule] ⏸ API binding (NOT IMPLEMENTED)" << std::endl;
    // if (!bindAPIs()) {
    //     return false;
    // }

    // Step 8: Initialize module
    // TODO: Call module entry point
    // - Provide WardenFuncList callback pointers
    // - Get module's exported functions
    std::cout << "[WardenModule] ⏸ Module initialization (NOT IMPLEMENTED)" << std::endl;
    // if (!initializeModule()) {
    //     return false;
    // }

    // For now, module "loading" succeeds at crypto layer only
    // Real execution would require all TODO items above
    loaded_ = false; // Set to false until full implementation

    std::cout << "[WardenModule] ⚠ Module loaded at CRYPTO LAYER ONLY" << std::endl;
    std::cout << "[WardenModule]   Native code execution NOT implemented" << std::endl;
    std::cout << "[WardenModule]   Check responses will be FAKED (fails strict servers)" << std::endl;

    return true; // Return true to indicate crypto layer succeeded
}

bool WardenModule::processCheckRequest(const std::vector<uint8_t>& checkData,
                                       std::vector<uint8_t>& responseOut) {
    if (!loaded_) {
        std::cerr << "[WardenModule] Module not loaded, cannot process checks" << std::endl;
        return false;
    }

    // TODO: Call module's PacketHandler function
    // This would execute native x86 code to:
    // - Parse check opcodes (0xF3 MEM_CHECK, 0xB2 PAGE_CHECK, etc.)
    // - Read actual memory from process
    // - Compute real SHA1 hashes
    // - Scan MPQ files
    // - Generate authentic response data

    std::cout << "[WardenModule] ⚠ processCheckRequest NOT IMPLEMENTED" << std::endl;
    std::cout << "[WardenModule]   Would call module->PacketHandler() here" << std::endl;

    // For now, return false to fall back to fake responses in GameHandler
    return false;
}

uint32_t WardenModule::tick(uint32_t deltaMs) {
    if (!loaded_ || !funcList_.tick) {
        return 0; // No tick needed
    }

    // TODO: Call module's Tick function
    // return funcList_.tick(deltaMs);

    return 0;
}

void WardenModule::generateRC4Keys(uint8_t* packet) {
    if (!loaded_ || !funcList_.generateRC4Keys) {
        return;
    }

    // TODO: Call module's GenerateRC4Keys function
    // This re-keys the Warden crypto stream
    // funcList_.generateRC4Keys(packet);
}

void WardenModule::unload() {
    if (moduleMemory_) {
        // TODO: Free executable memory region
        // - Call module's Unload() function first
        // - Free allocated memory
        // - Clear function pointers
        moduleMemory_ = nullptr;
    }

    loaded_ = false;
    moduleData_.clear();
    decryptedData_.clear();
    decompressedData_.clear();
}

// ============================================================================
// Private Validation Methods
// ============================================================================

bool WardenModule::verifyMD5(const std::vector<uint8_t>& data,
                             const std::vector<uint8_t>& expectedHash) {
    std::vector<uint8_t> computedHash = auth::Crypto::md5(data);

    if (computedHash.size() != expectedHash.size()) {
        return false;
    }

    return std::memcmp(computedHash.data(), expectedHash.data(), expectedHash.size()) == 0;
}

bool WardenModule::decryptRC4(const std::vector<uint8_t>& encrypted,
                              const std::vector<uint8_t>& key,
                              std::vector<uint8_t>& decryptedOut) {
    if (key.size() != 16) {
        std::cerr << "[WardenModule] Invalid RC4 key size: " << key.size() << " (expected 16)" << std::endl;
        return false;
    }

    // Initialize RC4 state (KSA - Key Scheduling Algorithm)
    std::vector<uint8_t> S(256);
    for (int i = 0; i < 256; ++i) {
        S[i] = i;
    }

    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + S[i] + key[i % key.size()]) % 256;
        std::swap(S[i], S[j]);
    }

    // Decrypt using RC4 (PRGA - Pseudo-Random Generation Algorithm)
    decryptedOut.resize(encrypted.size());
    int i = 0;
    j = 0;

    for (size_t idx = 0; idx < encrypted.size(); ++idx) {
        i = (i + 1) % 256;
        j = (j + S[i]) % 256;
        std::swap(S[i], S[j]);
        uint8_t K = S[(S[i] + S[j]) % 256];
        decryptedOut[idx] = encrypted[idx] ^ K;
    }

    return true;
}

bool WardenModule::verifyRSASignature(const std::vector<uint8_t>& data) {
    // RSA-2048 signature is last 256 bytes
    if (data.size() < 256) {
        std::cerr << "[WardenModule] Data too small for RSA signature (need at least 256 bytes)" << std::endl;
        return false;
    }

    // Extract signature (last 256 bytes)
    std::vector<uint8_t> signature(data.end() - 256, data.end());

    // Extract data without signature
    std::vector<uint8_t> dataWithoutSig(data.begin(), data.end() - 256);

    // Hardcoded WoW 3.3.5a Warden RSA public key
    // Exponent: 0x010001 (65537)
    const uint32_t exponent = 0x010001;

    // Modulus (256 bytes) - This is the actual public key from WoW 3.3.5a client
    // TODO: Extract this from WoW.exe binary at offset (varies by build)
    // For now, using a placeholder that will fail verification
    // To get the real modulus: extract from WoW.exe using a hex editor or IDA Pro
    const uint8_t modulus[256] = {
        // PLACEHOLDER - Replace with actual modulus from WoW 3.3.5a (build 12340)
        // This can be extracted from the WoW client binary
        // The actual value varies by client version and build
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // Compute expected hash: SHA1(data_without_sig + "MAIEV.MOD")
    std::vector<uint8_t> dataToHash = dataWithoutSig;
    const char* suffix = "MAIEV.MOD";
    dataToHash.insert(dataToHash.end(), suffix, suffix + strlen(suffix));

    std::vector<uint8_t> expectedHash = auth::Crypto::sha1(dataToHash);

    // Create RSA public key structure
    RSA* rsa = RSA_new();
    if (!rsa) {
        std::cerr << "[WardenModule] Failed to create RSA structure" << std::endl;
        return false;
    }

    BIGNUM* n = BN_bin2bn(modulus, 256, nullptr);
    BIGNUM* e = BN_new();
    BN_set_word(e, exponent);

    #if OPENSSL_VERSION_NUMBER >= 0x10100000L
        // OpenSSL 1.1.0+
        RSA_set0_key(rsa, n, e, nullptr);
    #else
        // OpenSSL 1.0.x
        rsa->n = n;
        rsa->e = e;
    #endif

    // Decrypt signature using public key
    std::vector<uint8_t> decryptedSig(256);
    int decryptedLen = RSA_public_decrypt(
        256,
        signature.data(),
        decryptedSig.data(),
        rsa,
        RSA_NO_PADDING
    );

    RSA_free(rsa);

    if (decryptedLen < 0) {
        std::cerr << "[WardenModule] RSA public decrypt failed" << std::endl;
        return false;
    }

    // Expected format: padding (0xBB bytes) + SHA1 hash (20 bytes)
    // Total: 256 bytes decrypted
    // Find SHA1 hash in decrypted signature (should be at end, preceded by 0xBB padding)

    // Look for SHA1 hash in last 20 bytes
    if (decryptedLen >= 20) {
        std::vector<uint8_t> actualHash(decryptedSig.end() - 20, decryptedSig.end());

        if (std::memcmp(actualHash.data(), expectedHash.data(), 20) == 0) {
            std::cout << "[WardenModule] ✓ RSA signature verified" << std::endl;
            return true;
        }
    }

    std::cerr << "[WardenModule] RSA signature verification FAILED (hash mismatch)" << std::endl;
    std::cerr << "[WardenModule] NOTE: Using placeholder modulus - extract real modulus from WoW.exe for actual verification" << std::endl;

    // For development, return true to proceed (since we don't have real modulus)
    // TODO: Set to false once real modulus is extracted
    std::cout << "[WardenModule] ⚠ Skipping RSA verification (placeholder modulus)" << std::endl;
    return true; // TEMPORARY - change to false for production
}

bool WardenModule::decompressZlib(const std::vector<uint8_t>& compressed,
                                  std::vector<uint8_t>& decompressedOut) {
    if (compressed.size() < 4) {
        std::cerr << "[WardenModule] Compressed data too small (need at least 4 bytes for size header)" << std::endl;
        return false;
    }

    // Read 4-byte uncompressed size (little-endian)
    uint32_t uncompressedSize =
        compressed[0] |
        (compressed[1] << 8) |
        (compressed[2] << 16) |
        (compressed[3] << 24);

    std::cout << "[WardenModule] Uncompressed size: " << uncompressedSize << " bytes" << std::endl;

    // Sanity check (modules shouldn't be larger than 10MB)
    if (uncompressedSize > 10 * 1024 * 1024) {
        std::cerr << "[WardenModule] Uncompressed size suspiciously large: " << uncompressedSize << " bytes" << std::endl;
        return false;
    }

    // Allocate output buffer
    decompressedOut.resize(uncompressedSize);

    // Setup zlib stream
    z_stream stream = {};
    stream.next_in = const_cast<uint8_t*>(compressed.data() + 4); // Skip 4-byte size header
    stream.avail_in = compressed.size() - 4;
    stream.next_out = decompressedOut.data();
    stream.avail_out = uncompressedSize;

    // Initialize inflater
    int ret = inflateInit(&stream);
    if (ret != Z_OK) {
        std::cerr << "[WardenModule] inflateInit failed: " << ret << std::endl;
        return false;
    }

    // Decompress
    ret = inflate(&stream, Z_FINISH);

    // Cleanup
    inflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        std::cerr << "[WardenModule] inflate failed: " << ret << std::endl;
        return false;
    }

    std::cout << "[WardenModule] ✓ zlib decompression successful ("
              << stream.total_out << " bytes decompressed)" << std::endl;

    return true;
}

bool WardenModule::parseExecutableFormat(const std::vector<uint8_t>& exeData) {
    // TODO: Parse custom skip/copy executable format
    //
    // Format:
    // [4 bytes] Final code size
    // [2 bytes] Skip section length
    // [N bytes] Code to skip
    // [2 bytes] Copy section length
    // [M bytes] Code to copy
    // ... (alternating skip/copy until end)
    //
    // Allocate moduleMemory_ and populate with copy sections

    return false; // Not implemented
}

bool WardenModule::applyRelocations() {
    // TODO: Fix absolute address references
    // - Delta-encoded offsets (multi-byte with high-bit continuation)
    // - Update pointers relative to moduleMemory_ base address
    return false; // Not implemented
}

bool WardenModule::bindAPIs() {
    // TODO: Resolve Windows API imports
    // - kernel32.dll: VirtualAlloc, VirtualProtect, GetTickCount, etc.
    // - user32.dll: GetForegroundWindow, etc.
    // - Patch import table in moduleMemory_
    return false; // Not implemented
}

bool WardenModule::initializeModule() {
    // TODO: Call module entry point
    // - Pass structure with 7 callback pointers:
    //   * Packet transmission
    //   * Module validation
    //   * Memory allocation (malloc/free)
    //   * RC4 key management
    // - Receive WardenFuncList with 4 exported functions
    // - Store in funcList_
    return false; // Not implemented
}

// ============================================================================
// WardenModuleManager Implementation
// ============================================================================

WardenModuleManager::WardenModuleManager() {
    // Set cache directory: ~/.local/share/wowee/warden_cache/
    const char* home = std::getenv("HOME");
    if (home) {
        cacheDirectory_ = std::string(home) + "/.local/share/wowee/warden_cache";
    } else {
        cacheDirectory_ = "./warden_cache";
    }

    // Create cache directory if it doesn't exist
    std::filesystem::create_directories(cacheDirectory_);

    std::cout << "[WardenModuleManager] Cache directory: " << cacheDirectory_ << std::endl;
}

WardenModuleManager::~WardenModuleManager() {
    modules_.clear();
}

bool WardenModuleManager::hasModule(const std::vector<uint8_t>& md5Hash) {
    // Check in-memory cache
    if (modules_.find(md5Hash) != modules_.end()) {
        return modules_[md5Hash]->isLoaded();
    }

    // Check disk cache
    std::vector<uint8_t> dummy;
    return loadCachedModule(md5Hash, dummy);
}

std::shared_ptr<WardenModule> WardenModuleManager::getModule(const std::vector<uint8_t>& md5Hash) {
    auto it = modules_.find(md5Hash);
    if (it != modules_.end()) {
        return it->second;
    }

    // Create new module instance
    auto module = std::make_shared<WardenModule>();
    modules_[md5Hash] = module;
    return module;
}

bool WardenModuleManager::receiveModuleChunk(const std::vector<uint8_t>& md5Hash,
                                             const std::vector<uint8_t>& chunkData,
                                             bool isComplete) {
    // Append to download buffer
    std::vector<uint8_t>& buffer = downloadBuffer_[md5Hash];
    buffer.insert(buffer.end(), chunkData.begin(), chunkData.end());

    std::cout << "[WardenModuleManager] Received chunk (" << chunkData.size()
              << " bytes, total: " << buffer.size() << ")" << std::endl;

    if (isComplete) {
        std::cout << "[WardenModuleManager] Module download complete ("
                  << buffer.size() << " bytes)" << std::endl;

        // Cache to disk
        cacheModule(md5Hash, buffer);

        // Clear download buffer
        downloadBuffer_.erase(md5Hash);

        return true;
    }

    return true;
}

bool WardenModuleManager::cacheModule(const std::vector<uint8_t>& md5Hash,
                                      const std::vector<uint8_t>& moduleData) {
    std::string cachePath = getCachePath(md5Hash);

    std::ofstream file(cachePath, std::ios::binary);
    if (!file) {
        std::cerr << "[WardenModuleManager] Failed to write cache: " << cachePath << std::endl;
        return false;
    }

    file.write(reinterpret_cast<const char*>(moduleData.data()), moduleData.size());
    file.close();

    std::cout << "[WardenModuleManager] Cached module to: " << cachePath << std::endl;
    return true;
}

bool WardenModuleManager::loadCachedModule(const std::vector<uint8_t>& md5Hash,
                                           std::vector<uint8_t>& moduleDataOut) {
    std::string cachePath = getCachePath(md5Hash);

    if (!std::filesystem::exists(cachePath)) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    moduleDataOut.resize(fileSize);
    file.read(reinterpret_cast<char*>(moduleDataOut.data()), fileSize);
    file.close();

    std::cout << "[WardenModuleManager] Loaded cached module (" << fileSize << " bytes)" << std::endl;
    return true;
}

std::string WardenModuleManager::getCachePath(const std::vector<uint8_t>& md5Hash) {
    // Convert MD5 hash to hex string for filename
    std::string hexHash;
    hexHash.reserve(md5Hash.size() * 2);

    for (uint8_t byte : md5Hash) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", byte);
        hexHash += buf;
    }

    return cacheDirectory_ + "/" + hexHash + ".wdn";
}

} // namespace game
} // namespace wowee
