#include "game/warden_crypto.hpp"
#include "core/logger.hpp"
#include <cstring>
#include <algorithm>

namespace wowee {
namespace game {

// Warden module keys for WoW 3.3.5a (from client analysis)
// These are the standard keys used by most 3.3.5a servers
static const uint8_t WARDEN_MODULE_KEY[16] = {
    0xC5, 0x35, 0xB2, 0x1E, 0xF8, 0xE7, 0x9F, 0x4B,
    0x91, 0xB6, 0xD1, 0x34, 0xA7, 0x2F, 0x58, 0x8C
};

WardenCrypto::WardenCrypto()
    : initialized_(false)
    , inputRC4_i_(0)
    , inputRC4_j_(0)
    , outputRC4_i_(0)
    , outputRC4_j_(0) {
}

WardenCrypto::~WardenCrypto() {
}

bool WardenCrypto::initialize(const std::vector<uint8_t>& moduleData) {
    if (moduleData.empty()) {
        LOG_ERROR("Warden: Cannot initialize with empty module data");
        return false;
    }

    LOG_INFO("Warden: Initializing crypto with ", moduleData.size(), " byte module");

    // Warden 3.3.5a module format (typically):
    // [1 byte opcode][16 bytes seed/key][remaining bytes = encrypted module data]

    if (moduleData.size() < 17) {
        LOG_WARNING("Warden: Module too small (", moduleData.size(), " bytes), using default keys");
        // Use default keys
        inputKey_.assign(WARDEN_MODULE_KEY, WARDEN_MODULE_KEY + 16);
        outputKey_.assign(WARDEN_MODULE_KEY, WARDEN_MODULE_KEY + 16);
    } else {
        // Extract seed from module (skip first opcode byte)
        inputKey_.assign(moduleData.begin() + 1, moduleData.begin() + 17);
        outputKey_ = inputKey_;

        // XOR with module key for output
        for (size_t i = 0; i < 16; ++i) {
            outputKey_[i] ^= WARDEN_MODULE_KEY[i];
        }

        LOG_INFO("Warden: Extracted 16-byte seed from module");
    }

    // Initialize RC4 ciphers
    inputRC4State_.resize(256);
    outputRC4State_.resize(256);

    initRC4(inputKey_, inputRC4State_, inputRC4_i_, inputRC4_j_);
    initRC4(outputKey_, outputRC4State_, outputRC4_i_, outputRC4_j_);

    initialized_ = true;
    LOG_INFO("Warden: Crypto initialized successfully");
    return true;
}

std::vector<uint8_t> WardenCrypto::decrypt(const std::vector<uint8_t>& data) {
    if (!initialized_) {
        LOG_WARNING("Warden: Attempted to decrypt without initialization");
        return data;
    }

    std::vector<uint8_t> result(data.size());
    processRC4(data.data(), result.data(), data.size(),
               inputRC4State_, inputRC4_i_, inputRC4_j_);
    return result;
}

std::vector<uint8_t> WardenCrypto::encrypt(const std::vector<uint8_t>& data) {
    if (!initialized_) {
        LOG_WARNING("Warden: Attempted to encrypt without initialization");
        return data;
    }

    std::vector<uint8_t> result(data.size());
    processRC4(data.data(), result.data(), data.size(),
               outputRC4State_, outputRC4_i_, outputRC4_j_);
    return result;
}

void WardenCrypto::initRC4(const std::vector<uint8_t>& key,
                            std::vector<uint8_t>& state,
                            uint8_t& i, uint8_t& j) {
    // Initialize permutation
    for (int idx = 0; idx < 256; ++idx) {
        state[idx] = static_cast<uint8_t>(idx);
    }

    // Key scheduling algorithm (KSA)
    j = 0;
    for (int idx = 0; idx < 256; ++idx) {
        j = (j + state[idx] + key[idx % key.size()]) & 0xFF;
        std::swap(state[idx], state[j]);
    }

    i = 0;
    j = 0;
}

void WardenCrypto::processRC4(const uint8_t* input, uint8_t* output, size_t length,
                               std::vector<uint8_t>& state, uint8_t& i, uint8_t& j) {
    for (size_t idx = 0; idx < length; ++idx) {
        i = (i + 1) & 0xFF;
        j = (j + state[i]) & 0xFF;
        std::swap(state[i], state[j]);

        uint8_t k = state[(state[i] + state[j]) & 0xFF];
        output[idx] = input[idx] ^ k;
    }
}

} // namespace game
} // namespace wowee
