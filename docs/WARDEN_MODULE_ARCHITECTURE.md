# Warden Module Execution Architecture

**Status**: Foundation implemented, execution layer TODO
**Created**: 2026-02-12
**Version**: WoW 3.3.5a (build 12340)

---

## Overview

This document describes the **Warden module execution architecture** - a system for loading and running native x86 Warden anti-cheat modules sent by WoW servers.

**IMPORTANT**: This is a **foundation implementation**. Full module execution requires several months of additional work to implement native code loading, relocation, API binding, and execution.

---

## Architecture Layers

The system is built in three layers:

```
┌─────────────────────────────────────┐
│   GameHandler (Protocol Layer)     │  Handles SMSG_WARDEN_DATA packets
│  - Receives Warden packets          │  Routes to module manager
│  - Delegates to WardenModuleManager │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  WardenModuleManager (Lifecycle)    │  Manages multiple modules
│  - Module caching (disk)            │  Handles downloads
│  - Module lookup by MD5 hash        │  Coordinates loading
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   WardenModule (Execution)          │  Individual module instance
│  ✅ MD5 verification                 │  Validates module data
│  ✅ RC4 decryption                   │  Uses WardenCrypto
│  ❌ RSA signature (TODO)             │  Public key verification
│  ❌ zlib decompression (TODO)        │  Inflate compressed code
│  ❌ Executable parsing (TODO)        │  Skip/copy sections
│  ❌ Address relocation (TODO)        │  Fix absolute references
│  ❌ API binding (TODO)                │  Resolve kernel32.dll imports
│  ❌ Native execution (TODO)          │  Run x86 code callbacks
└─────────────────────────────────────┘
```

---

## File Structure

```
include/game/warden_module.hpp       - Module loader interface
src/game/warden_module.cpp           - Implementation (stubs for TODOs)
include/game/game_handler.hpp        - Added WardenModuleManager member
src/game/game_handler.cpp            - Initializes module manager
include/game/warden_crypto.hpp       - RC4 crypto (existing, reused)
src/game/warden_crypto.cpp           - RC4 implementation (existing)
```

---

## Classes

### WardenModule

Represents a single loaded Warden module.

#### Public Interface

```cpp
class WardenModule {
public:
    // Load module from encrypted data
    bool load(const std::vector<uint8_t>& moduleData,
              const std::vector<uint8_t>& md5Hash,
              const std::vector<uint8_t>& rc4Key);

    // Check if module is ready for execution
    bool isLoaded() const;

    // Process check request (calls module's PacketHandler)
    bool processCheckRequest(const std::vector<uint8_t>& checkData,
                            std::vector<uint8_t>& responseOut);

    // Periodic tick (calls module's Tick function)
    uint32_t tick(uint32_t deltaMs);

    // Re-key crypto (called by server opcode 0x05)
    void generateRC4Keys(uint8_t* packet);

    // Cleanup and unload
    void unload();
};
```

#### Loading Pipeline

The `load()` function executes 8 steps:

```
Step 1: Verify MD5      ✅ Implemented (uses auth::Crypto::md5)
Step 2: RC4 Decrypt     ✅ Implemented (uses WardenCrypto)
Step 3: RSA Verify      ❌ TODO (requires OpenSSL RSA-2048)
Step 4: zlib Decompress ❌ TODO (requires zlib library)
Step 5: Parse Exe       ❌ TODO (custom skip/copy format)
Step 6: Relocations     ❌ TODO (delta-encoded offsets)
Step 7: Bind APIs       ❌ TODO (kernel32.dll, user32.dll imports)
Step 8: Initialize      ❌ TODO (call module entry point)
```

**Current Behavior**: Steps 1-2 succeed, steps 3-8 are logged as "NOT IMPLEMENTED" and return without error. Module is marked as NOT loaded (`loaded_ = false`).

---

### WardenFuncList

Callback functions exported by loaded module.

```cpp
struct WardenFuncList {
    GenerateRC4KeysFunc generateRC4Keys;  // Re-key crypto stream
    UnloadFunc unload;                     // Cleanup before unload
    PacketHandlerFunc packetHandler;       // Process check requests
    TickFunc tick;                         // Periodic execution
};
```

These are **function pointers** that would be populated by calling the module's initialization entry point after loading.

**Current Status**: All callbacks are `nullptr` (not initialized).

---

### WardenModuleManager

Manages module lifecycle and caching.

#### Public Interface

```cpp
class WardenModuleManager {
public:
    // Check if module is cached locally
    bool hasModule(const std::vector<uint8_t>& md5Hash);

    // Get or create module instance
    std::shared_ptr<WardenModule> getModule(const std::vector<uint8_t>& md5Hash);

    // Receive module data chunk (multi-packet download)
    bool receiveModuleChunk(const std::vector<uint8_t>& md5Hash,
                           const std::vector<uint8_t>& chunkData,
                           bool isComplete);

    // Cache module to disk
    bool cacheModule(const std::vector<uint8_t>& md5Hash,
                     const std::vector<uint8_t>& moduleData);

    // Load cached module from disk
    bool loadCachedModule(const std::vector<uint8_t>& md5Hash,
                         std::vector<uint8_t>& moduleDataOut);
};
```

#### Module Caching

Modules are cached at:
```
~/.local/share/wowee/warden_cache/<MD5_HASH>.wdn
```

Example:
```
~/.local/share/wowee/warden_cache/
  3a7f9b2e1c5d8a4f6e3b2c1d5e7f8a9b.wdn    # Module A
  8c4b2d1f5e3a7f9b1c2d3e4f5a6b7c8d.wdn    # Module B
```

**Cache Benefits**:
- Skip re-download on reconnect
- Faster server connections
- Persist across sessions

**Cache Invalidation**: Servers can send new modules with different MD5 hashes, which will be downloaded and cached separately.

---

## Integration with GameHandler

### Initialization

```cpp
GameHandler::GameHandler() {
    // ... other initialization ...

    // Initialize Warden module manager
    wardenModuleManager_ = std::make_unique<WardenModuleManager>();
}
```

### Future Integration (Not Yet Implemented)

When module execution is fully implemented, the flow would be:

```cpp
void GameHandler::handleWardenData(network::Packet& packet) {
    // First packet: module download request
    if (is_module_packet) {
        auto module = wardenModuleManager_->getModule(md5Hash);
        module->load(moduleData, md5Hash, rc4Key);
        return;
    }

    // Subsequent packets: check requests
    auto decrypted = wardenCrypto_->decrypt(packet.getData());

    // Try module execution first
    std::vector<uint8_t> response;
    if (module->processCheckRequest(decrypted, response)) {
        // Module generated authentic response
        auto encrypted = wardenCrypto_->encrypt(response);
        sendResponse(encrypted);
    } else {
        // Fall back to fake responses (current behavior)
        generateFakeResponse(decrypted, response);
        auto encrypted = wardenCrypto_->encrypt(response);
        sendResponse(encrypted);
    }
}
```

---

## Module Packet Protocol

### Opcode 0x00 - Module Check Request

Server asks if client has module cached.

**Server → Client:**
```
[1 byte]   Opcode (0x00)
[16 bytes] Module MD5 hash (identifier)
[16 bytes] RC4 decryption key seed
[4 bytes]  Module compressed size
```

**Client → Server Response:**
```
[1 byte]  0x00 = need download
[1 byte]  0x01 = have cached, ready to use
```

### Opcode 0x01 - Module Data Transfer

Server sends encrypted module data in chunks.

**Server → Client:**
```
[1 byte]   Opcode (0x01)
[2 bytes]  Chunk length
[N bytes]  Encrypted module data
```

Multiple 0x01 packets sent until total bytes received equals size from opcode 0x00.

**Client → Server Response:**
```
[1 byte]  0x01 = success
[1 byte]  0x00 = failure (request retransmit)
```

---

## Module File Format

### Encrypted Module Structure

```
┌────────────────────────────────────┐
│ RC4-Encrypted Module Data          │
│  (Key from server's 16-byte seed)  │
└────────────┬───────────────────────┘
             │ RC4 Decrypt
┌────────────▼───────────────────────┐
│ Decrypted Module                   │
│ ┌────────────────────────────────┐ │
│ │ [4 bytes] Uncompressed size    │ │
│ │ [variable] zlib compressed data│ │
│ │ [4 bytes] "SIGN" or "NGIS"     │ │
│ │ [256 bytes] RSA-2048 signature │ │
│ └────────────────────────────────┘ │
└────────────┬───────────────────────┘
             │ zlib inflate
┌────────────▼───────────────────────┐
│ Decompressed Executable            │
│ ┌────────────────────────────────┐ │
│ │ [4 bytes] Final code size      │ │
│ │ [2 bytes] Skip section length  │ │
│ │ [N bytes] Code to skip         │ │
│ │ [2 bytes] Copy section length  │ │
│ │ [M bytes] Code to copy (x86)   │ │
│ │ ... (alternating skip/copy)    │ │
│ └────────────────────────────────┘ │
└────────────────────────────────────┘
```

### RSA Signature Verification

**Public Key** (hardcoded in WoW client):
```
Exponent: {0x01, 0x00, 0x01, 0x00}  (Little-endian 65537)
Modulus:  256-byte value (in client binary)
```

**Expected Signature**:
```
SHA1(module_data + "MAIEV.MOD") padded with 0xBB bytes
```

---

## Implementation Roadmap

### Phase 1: Crypto Layer (COMPLETED ✅)

- [x] RC4 encryption/decryption (WardenCrypto)
- [x] MD5 hash verification
- [x] SHA1 hashing
- [x] Module seed extraction

### Phase 2: Foundation (CURRENT - JUST COMPLETED ✅)

- [x] WardenModule class skeleton
- [x] WardenModuleManager class
- [x] Module caching system
- [x] Integration with GameHandler
- [x] Build system integration
- [x] Comprehensive documentation

### Phase 3: Validation Layer (TODO - 1-2 weeks)

- [ ] Implement RSA-2048 signature verification
  - OpenSSL RSA_public_decrypt
  - Hardcode public key modulus
  - Verify SHA1(data + "MAIEV.MOD") signature
- [ ] Implement zlib decompression
  - Link against zlib library
  - Read 4-byte uncompressed size
  - Inflate compressed stream
- [ ] Add detailed error reporting for failures

### Phase 4: Executable Loader (TODO - 2-3 weeks)

- [ ] Parse custom skip/copy executable format
  - Read alternating skip/copy sections (2-byte length + data)
  - Allocate executable memory region
  - Copy code sections to memory
- [ ] Implement address relocation
  - Parse delta-encoded offsets (multi-byte with high-bit continuation)
  - Fix absolute references relative to module base address
  - Update pointer tables
- [ ] Set memory permissions (VirtualProtect equivalent)

### Phase 5: API Binding (TODO - 1 week)

- [ ] Resolve Windows API imports
  - kernel32.dll: VirtualAlloc, VirtualProtect, GetTickCount, etc.
  - user32.dll: GetForegroundWindow, etc.
- [ ] Patch import address table (IAT)
- [ ] Provide callback structure to module
  - Packet transmission functions
  - Memory allocation (malloc/free)
  - RC4 key management

### Phase 6: Execution Engine (TODO - 2-3 weeks)

- [ ] Call module initialization entry point
- [ ] Receive WardenFuncList callbacks
- [ ] Implement PacketHandler dispatcher
  - Route check opcodes (0xF3, 0xB2, 0x98, etc.)
  - Let module perform REAL memory scans
  - Return authentic responses
- [ ] Implement Tick() periodic calls
- [ ] Implement GenerateRC4Keys() re-keying
- [ ] Implement Unload() cleanup

### Phase 7: Testing & Refinement (TODO - 1-2 weeks)

- [ ] Test against Warmane (strict enforcement)
- [ ] Test against local AzerothCore (permissive)
- [ ] Debug module execution issues
- [ ] Add comprehensive logging
- [ ] Performance optimization
- [ ] Memory safety validation

---

## Estimated Timeline

| Phase | Duration | Difficulty |
|-------|----------|------------|
| Phase 1: Crypto | ✅ DONE | ⭐⭐ |
| Phase 2: Foundation | ✅ DONE | ⭐ |
| Phase 3: Validation | 1-2 weeks | ⭐⭐⭐ |
| Phase 4: Executable Loader | 2-3 weeks | ⭐⭐⭐⭐⭐ |
| Phase 5: API Binding | 1 week | ⭐⭐⭐ |
| Phase 6: Execution Engine | 2-3 weeks | ⭐⭐⭐⭐⭐ |
| Phase 7: Testing | 1-2 weeks | ⭐⭐⭐⭐ |
| **TOTAL** | **2-3 months** | **Very High** |

---

## Alternative: Packet Capture Approach

Instead of full module execution, capture responses from real WoW client:

### Process

1. Run real WoW 3.3.5a client
2. Connect to Warmane with Wireshark running
3. Capture CMSG_WARDEN_DATA response packets
4. Analyze response format
5. Implement matching response generator in wowee

### Benefits

- Much faster (1-2 weeks vs 2-3 months)
- Lower complexity
- May work if servers don't require full execution

### Drawbacks

- Only works if response format is static
- May not work if modules change per session
- Server might detect pattern-based responses
- Doesn't scale to different servers

---

## Current Behavior

With the foundation implemented, the system:

1. ✅ Initializes WardenModuleManager on startup
2. ✅ Receives SMSG_WARDEN_DATA packets
3. ✅ Logs module structure (opcode, seed, trailing data)
4. ✅ Verifies MD5 hash (step 1)
5. ✅ RC4 decrypts module data (step 2)
6. ⚠️ Logs "NOT IMPLEMENTED" for steps 3-8
7. ❌ Falls back to **fake responses** (current GameHandler behavior)
8. ❌ Warmane rejects fake responses (server goes silent)

**For strict servers like Warmane**: Module execution (Phases 3-7) is REQUIRED.

**For permissive servers**: Current fake responses work without module execution.

---

## Code Examples

### Creating a Module Instance

```cpp
auto moduleManager = std::make_unique<WardenModuleManager>();

// Check if module cached
std::vector<uint8_t> md5Hash = { /* 16 bytes */ };
if (moduleManager->hasModule(md5Hash)) {
    std::cout << "Module cached, loading..." << std::endl;
    std::vector<uint8_t> moduleData;
    moduleManager->loadCachedModule(md5Hash, moduleData);
}

// Get module instance
auto module = moduleManager->getModule(md5Hash);

// Load module
std::vector<uint8_t> rc4Key = { /* 16 bytes */ };
if (module->load(moduleData, md5Hash, rc4Key)) {
    std::cout << "Module loaded successfully" << std::endl;
} else {
    std::cout << "Module load failed" << std::endl;
}
```

### Processing Check Requests (Future)

```cpp
// Decrypt incoming check request
std::vector<uint8_t> decrypted = wardenCrypto_->decrypt(packet.getData());

// Try module execution
std::vector<uint8_t> response;
if (module->isLoaded() && module->processCheckRequest(decrypted, response)) {
    // Module generated authentic response
    std::cout << "Module executed checks, got real response" << std::endl;
} else {
    // Fall back to fake responses
    std::cout << "Module not loaded or failed, using fake response" << std::endl;
    response = generateFakeResponse(decrypted);
}

// Encrypt and send
auto encrypted = wardenCrypto_->encrypt(response);
sendWardenResponse(encrypted);
```

---

## References

### Documentation
- [WARDEN_IMPLEMENTATION.md](WARDEN_IMPLEMENTATION.md) - Testing and findings
- [WARDEN_QUICK_REFERENCE.md](WARDEN_QUICK_REFERENCE.md) - Quick troubleshooting guide
- [WoWDev Wiki - Warden](https://wowdev.wiki/Warden)
- [Exploiting Warden Behaviour](https://jordanwhittle.com/posts/exploiting-warden/)

### Source Code References
- [MaNGOS Two - Warden.cpp](https://github.com/mangostwo/server/blob/master/src/game/Warden/Warden.cpp)
- [TrinityCore Warden](https://github.com/TrinityCore/TrinityCore/tree/3.3.5/src/server/game/Warden)

### Implementation Files
- `include/game/warden_module.hpp` - Module loader interface
- `src/game/warden_module.cpp` - Implementation (phase 2 complete)
- `include/game/warden_crypto.hpp` - RC4 crypto (existing, reused)
- `src/game/warden_crypto.cpp` - RC4 implementation (existing)

---

**Last Updated**: 2026-02-12
**Status**: Phase 2 (Foundation) COMPLETE
**Next Step**: Phase 3 (Validation Layer) or Alternative (Packet Capture)
