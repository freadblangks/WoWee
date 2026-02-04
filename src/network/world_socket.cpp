#include "network/world_socket.hpp"
#include "network/packet.hpp"
#include "network/net_platform.hpp"
#include "auth/crypto.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace network {

// WoW 3.3.5a RC4 encryption keys (hardcoded in client)
static const uint8_t ENCRYPT_KEY[] = {
    0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5,
    0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE
};

static const uint8_t DECRYPT_KEY[] = {
    0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA,
    0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57
};

WorldSocket::WorldSocket() {
    net::ensureInit();
}

WorldSocket::~WorldSocket() {
    disconnect();
}

bool WorldSocket::connect(const std::string& host, uint16_t port) {
    LOG_INFO("Connecting to world server: ", host, ":", port);

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCK) {
        LOG_ERROR("Failed to create socket");
        return false;
    }

    // Set non-blocking
    net::setNonBlocking(sockfd);

    // Resolve host
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
        LOG_ERROR("Failed to resolve host: ", host);
        net::closeSocket(sockfd);
        sockfd = INVALID_SOCK;
        return false;
    }

    // Connect
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
    serverAddr.sin_port = htons(port);

    int result = ::connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result < 0) {
        int err = net::lastError();
        if (!net::isInProgress(err)) {
            LOG_ERROR("Failed to connect: ", net::errorString(err));
            net::closeSocket(sockfd);
            sockfd = INVALID_SOCK;
            return false;
        }
    }

    connected = true;
    LOG_INFO("Connected to world server: ", host, ":", port);
    return true;
}

void WorldSocket::disconnect() {
    if (sockfd != INVALID_SOCK) {
        net::closeSocket(sockfd);
        sockfd = INVALID_SOCK;
    }
    connected = false;
    encryptionEnabled = false;
    receiveBuffer.clear();
    LOG_INFO("Disconnected from world server");
}

bool WorldSocket::isConnected() const {
    return connected;
}

void WorldSocket::send(const Packet& packet) {
    if (!connected) return;

    const auto& data = packet.getData();
    uint16_t opcode = packet.getOpcode();
    uint16_t size = static_cast<uint16_t>(data.size());

    // Build header (6 bytes for outgoing): size(2) + opcode(4)
    std::vector<uint8_t> sendData;
    sendData.reserve(6 + size);

    // Size (2 bytes, big-endian) - payload size only, does NOT include header
    sendData.push_back((size >> 8) & 0xFF);
    sendData.push_back(size & 0xFF);

    // Opcode (4 bytes, big-endian)
    sendData.push_back((opcode >> 24) & 0xFF);
    sendData.push_back((opcode >> 16) & 0xFF);
    sendData.push_back((opcode >> 8) & 0xFF);
    sendData.push_back(opcode & 0xFF);

    // Encrypt header if encryption is enabled
    if (encryptionEnabled) {
        encryptCipher.process(sendData.data(), 6);
        LOG_DEBUG("Encrypted outgoing header: opcode=0x", std::hex, opcode, std::dec);
    }

    // Add payload (unencrypted)
    sendData.insert(sendData.end(), data.begin(), data.end());

    LOG_DEBUG("Sending world packet: opcode=0x", std::hex, opcode, std::dec,
              " size=", size, " bytes (", sendData.size(), " total)");

    // Send complete packet
    ssize_t sent = net::portableSend(sockfd, sendData.data(), sendData.size());
    if (sent < 0) {
        LOG_ERROR("Send failed: ", net::errorString(net::lastError()));
    } else if (static_cast<size_t>(sent) != sendData.size()) {
        LOG_WARNING("Partial send: ", sent, " of ", sendData.size(), " bytes");
    }
}

void WorldSocket::update() {
    if (!connected) return;

    // Receive data into buffer
    uint8_t buffer[4096];
    ssize_t received = net::portableRecv(sockfd, buffer, sizeof(buffer));

    if (received > 0) {
        LOG_DEBUG("Received ", received, " bytes from world server");
        receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + received);

        // Try to parse complete packets from buffer
        tryParsePackets();
    }
    else if (received == 0) {
        LOG_INFO("World server connection closed");
        disconnect();
    }
    else {
        int err = net::lastError();
        if (!net::isWouldBlock(err)) {
            LOG_ERROR("Receive failed: ", net::errorString(err));
            disconnect();
        }
    }
}

void WorldSocket::tryParsePackets() {
    // World server packets have 4-byte incoming header: size(2) + opcode(2)
    while (receiveBuffer.size() >= 4) {
        // Copy header for decryption
        uint8_t header[4];
        memcpy(header, receiveBuffer.data(), 4);

        // Decrypt header if encryption is enabled
        if (encryptionEnabled) {
            decryptCipher.process(header, 4);
        }

        // Parse header (big-endian)
        uint16_t size = (header[0] << 8) | header[1];
        uint16_t opcode = (header[2] << 8) | header[3];

        // Total packet size: header(4) + payload(size)
        size_t totalSize = 4 + size;

        if (receiveBuffer.size() < totalSize) {
            // Not enough data yet
            LOG_DEBUG("Waiting for more data: have ", receiveBuffer.size(),
                     " bytes, need ", totalSize);
            break;
        }

        // We have a complete packet!
        LOG_DEBUG("Parsing world packet: opcode=0x", std::hex, opcode, std::dec,
                 " size=", size, " bytes");

        // Extract payload (skip header)
        std::vector<uint8_t> packetData(receiveBuffer.begin() + 4,
                                        receiveBuffer.begin() + totalSize);

        // Create packet with opcode and payload
        Packet packet(opcode, packetData);

        // Remove parsed data from buffer
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + totalSize);

        // Call callback if set
        if (packetCallback) {
            packetCallback(packet);
        }
    }
}

void WorldSocket::initEncryption(const std::vector<uint8_t>& sessionKey) {
    if (sessionKey.size() != 40) {
        LOG_ERROR("Invalid session key size: ", sessionKey.size(), " (expected 40)");
        return;
    }

    LOG_INFO("Initializing world server header encryption");

    // Convert hardcoded keys to vectors
    std::vector<uint8_t> encryptKey(ENCRYPT_KEY, ENCRYPT_KEY + 16);
    std::vector<uint8_t> decryptKey(DECRYPT_KEY, DECRYPT_KEY + 16);

    // Compute HMAC-SHA1(key, sessionKey) for each cipher
    std::vector<uint8_t> encryptHash = auth::Crypto::hmacSHA1(encryptKey, sessionKey);
    std::vector<uint8_t> decryptHash = auth::Crypto::hmacSHA1(decryptKey, sessionKey);

    LOG_DEBUG("Encrypt hash: ", encryptHash.size(), " bytes");
    LOG_DEBUG("Decrypt hash: ", decryptHash.size(), " bytes");

    // Initialize RC4 ciphers with HMAC results
    encryptCipher.init(encryptHash);
    decryptCipher.init(decryptHash);

    // Drop first 1024 bytes of keystream (WoW protocol requirement)
    encryptCipher.drop(1024);
    decryptCipher.drop(1024);

    encryptionEnabled = true;
    LOG_INFO("World server encryption initialized successfully");
}

} // namespace network
} // namespace wowee
