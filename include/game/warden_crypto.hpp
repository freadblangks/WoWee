#pragma once

#include <vector>
#include <cstdint>
#include <memory>

namespace wowee {
namespace game {

/**
 * Warden anti-cheat crypto handler for WoW 3.3.5a
 * Handles RC4 encryption/decryption of Warden packets
 */
class WardenCrypto {
public:
    WardenCrypto();
    ~WardenCrypto();

    /**
     * Initialize Warden crypto with module seed
     * @param moduleData The SMSG_WARDEN_DATA payload containing seed
     * @return true if initialization succeeded
     */
    bool initialize(const std::vector<uint8_t>& moduleData);

    /**
     * Decrypt an incoming Warden packet
     * @param data Encrypted data from server
     * @return Decrypted data
     */
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data);

    /**
     * Encrypt an outgoing Warden response
     * @param data Plaintext response data
     * @return Encrypted data
     */
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data);

    /**
     * Check if crypto has been initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    bool initialized_;
    std::vector<uint8_t> inputKey_;
    std::vector<uint8_t> outputKey_;

    // RC4 state for incoming packets
    std::vector<uint8_t> inputRC4State_;
    uint8_t inputRC4_i_;
    uint8_t inputRC4_j_;

    // RC4 state for outgoing packets
    std::vector<uint8_t> outputRC4State_;
    uint8_t outputRC4_i_;
    uint8_t outputRC4_j_;

    void initRC4(const std::vector<uint8_t>& key,
                 std::vector<uint8_t>& state,
                 uint8_t& i, uint8_t& j);

    void processRC4(const uint8_t* input, uint8_t* output, size_t length,
                    std::vector<uint8_t>& state, uint8_t& i, uint8_t& j);
};

} // namespace game
} // namespace wowee
