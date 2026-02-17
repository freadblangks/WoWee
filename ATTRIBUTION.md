# Attribution

This project was made possible by the work of the WoW modding and reverse-engineering community. We gratefully acknowledge the following resources and projects:

## Documentation

### WoWDev Wiki
https://wowdev.wiki/

The WoWDev Wiki is a community-maintained resource documenting World of Warcraft file formats and protocols. This project's file format parsers were implemented based on specifications from the wiki:

- [ADT (terrain)](https://wowdev.wiki/ADT) - Terrain tile format
- [M2 (models)](https://wowdev.wiki/M2) - Character and creature model format
- [WMO (buildings)](https://wowdev.wiki/WMO) - World model object format
- [BLP (textures)](https://wowdev.wiki/BLP) - Blizzard picture format
- [DBC (database)](https://wowdev.wiki/DBC) - Database client file format
- [World Packet](https://wowdev.wiki/World_Packet) - Network protocol opcodes

## Reference Implementations

### TrinityCore
https://github.com/TrinityCore/TrinityCore

Open-source WoW server emulator. Referenced for understanding server-side protocol behavior and packet structures.

### MaNGOS / CMaNGOS
https://github.com/cmangos/mangos-wotlk

Open-source WoW server emulator. Referenced for protocol documentation and authentication flow.

### AzerothCore
https://github.com/azerothcore/azerothcore-wotlk

Open-source WoW 3.3.5a server. Referenced for SRP6 authentication details, update field indices, and packet structures.

## Libraries

This project uses the following open-source libraries:

| Library | License | Purpose |
|---------|---------|---------|
| [SDL2](https://libsdl.org/) | zlib | Window management, input handling |
| [GLEW](http://glew.sourceforge.net/) | BSD/MIT | OpenGL extension loading |
| [GLM](https://github.com/g-truc/glm) | MIT | Mathematics library |
| [OpenSSL](https://www.openssl.org/) | Apache 2.0 | Cryptographic functions (SRP6, RC4, RSA) |
| [Unicorn Engine](https://www.unicorn-engine.org/) | LGPL 2 | x86 CPU emulation for Warden module execution |
| [miniaudio](https://miniaud.io/) | MIT/Unlicense | Audio playback |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | MIT | MPQ archive extraction |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | Immediate mode GUI |

## Cryptographic Standards

The SRP6 authentication implementation follows:
- [RFC 2945](https://tools.ietf.org/html/rfc2945) - The SRP Authentication and Key Exchange System
- [RFC 5054](https://tools.ietf.org/html/rfc5054) - Using SRP for TLS Authentication

## Legal Notice

World of Warcraft is a trademark of Blizzard Entertainment, Inc. This project is not affiliated with or endorsed by Blizzard Entertainment.

This project does not include any Blizzard Entertainment proprietary data, assets, or code. All file format parsers were implemented independently based on publicly available community documentation.

Users must supply their own legally obtained WoW 3.3.5a game data files to use this software.
