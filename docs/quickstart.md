# Quick Start Guide

## Current Status

Wowee is a **fully functional native C++ World of Warcraft 3.3.5a client**! The application includes:

✅ Complete SRP6a authentication system
✅ Character creation and selection
✅ Full 3D rendering engine (terrain, water, sky, characters, buildings, particles)
✅ Online multiplayer gameplay (movement, combat, spells, inventory, quests)
✅ Offline single-player mode
✅ Comprehensive UI system (action bar, spellbook, inventory, quest log, chat)
✅ Asset pipeline for all WoW formats (MPQ, BLP, M2, ADT, WMO, DBC)

## Quick Start

### Prerequisites

Ensure you have all dependencies installed (see main README.md for details).

### Build & Run

```bash
# Clone the repository
git clone https://github.com/yourname/wowee.git
cd wowee

# Get ImGui (required)
git clone https://github.com/ocornut/imgui.git extern/imgui

# Set up game data (see "Game Data" section in README.md)
# Either symlink Data/ directory or set WOW_DATA_PATH environment variable

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./bin/wowee
```

## What Works Right Now

### Authentication & Character Selection
- Login with username and password
- Realm selection with population info
- Character list with 3D preview
- Character creation (all races and classes)
- Enter world seamlessly

### In-Game Gameplay
- **Movement**: WASD movement with camera orbit
- **Combat**: Auto-attack, spell casting, damage calculation
- **Targeting**: Click-to-target, tab-cycling, faction-based hostility
- **Inventory**: Full equipment and backpack system with drag-drop
- **Spells**: Spellbook organized by class specialties, drag-drop to action bar
- **Quests**: Quest markers on NPCs and minimap, quest log, quest details, turn-in
- **Vendors**: Buy and sell items with gold tracking
- **Loot**: Loot window for items and gold
- **Chat**: Send and receive chat messages (SAY, YELL, WHISPER)
- **NPCs**: Gossip interactions, animations

### Rendering
- Multi-tile terrain streaming with async loading
- Animated water with reflections and refractions
- Dynamic day/night cycle with sun and moon
- Procedural star field (1000+ stars)
- Volumetric clouds with FBM noise
- Weather effects (rain and snow)
- Skeletal character animations (256 bones, GPU skinning)
- Building rendering (WMO) with frustum culling
- M2 particle emitters
- Post-processing (HDR, tonemapping, shadow mapping)

### Single-Player Mode
- Play offline without a server connection
- Local character persistence
- Simulated combat and XP
- Settings persistence

## Common Tasks

### Connecting to a Server

1. Launch wowee: `./bin/wowee`
2. Enter your username and password
3. Enter the auth server address (default: `localhost`)
4. Click "Login"
5. Select your realm from the list
6. Select or create a character
7. Click "Enter World"

### Single-Player Mode

1. Launch wowee
2. Click "Single Player" on the auth screen
3. Create or select a character
4. Play offline without a server connection

### Playing the Game

**Movement**:
- WASD to move
- Mouse to look around / orbit camera
- Shift for sprint

**Combat**:
- Left-click to target enemies
- Tab to cycle targets
- 1-9, 0, -, = to use action bar abilities
- Drag spells from spellbook (P) to action bar

**Inventory**:
- Press I to open inventory
- Drag items to equipment slots to equip
- Drag items to vendors to sell
- Drag items to action bar to use

**Quests**:
- Click NPCs with ! marker to get quests
- Press L to view quest log
- Click NPCs with ? marker to turn in quests
- Quest markers appear on minimap

**Chat**:
- Press Enter to open chat
- Type message and press Enter to send
- Chat commands: /say, /yell, /whisper [name]

### Development & Debugging

**Performance Monitoring**:
- Press F1 to toggle performance HUD
- Shows FPS, draw calls, triangle count, GPU usage

**Rendering Debug**:
- F2: Toggle wireframe mode
- F9: Toggle time progression
- F10: Toggle sun/moon
- F11: Toggle stars
- +/-: Manual time control
- C: Toggle clouds
- L: Toggle lens flare
- W: Cycle weather

**Settings**:
- Settings window available in-game
- Adjust UI opacity
- Configure graphics options

### Advanced Configuration

**Environment Variables**:
```bash
# Set custom WoW data path
export WOW_DATA_PATH="/path/to/WoW-3.3.5a/Data"

# Run with custom data path
WOW_DATA_PATH="/path/to/data" ./bin/wowee
```

**Server Configuration**:
Edit auth server address in the login screen or configure default in Application settings.

## Troubleshooting

### Connection Issues

**Problem**: Cannot connect to authentication server
- Check that the auth server is running and reachable
- Verify the server address and port (default: 3724)
- Check firewall settings

**Problem**: Disconnected during gameplay
- Network timeout or unstable connection
- Check server logs for errors
- Application will return to authentication screen

### Rendering Issues

**Problem**: Low FPS or stuttering
- Press F1 to check performance stats
- Reduce graphics settings in settings window
- Check GPU driver version

**Problem**: Missing textures or models
- Verify all WoW 3.3.5a MPQ files are present in Data/ directory
- Check that Data/ path is correct (or WOW_DATA_PATH is set)
- Look for errors in console output

**Problem**: Terrain not loading
- Async terrain streaming may take a moment
- Check for MPQ read errors in console
- Verify ADT files exist for the current map

### Gameplay Issues

**Problem**: Cannot interact with NPCs
- Ensure you're within interaction range (5-10 yards)
- Check that NPC is not in combat
- Left-click to target NPC first

**Problem**: Spells not working
- Check that you have enough mana/rage/energy
- Verify spell is on cooldown (check action bar)
- Ensure you have a valid target (for targeted spells)

**Problem**: Items not equipping
- Check that item is for your class
- Verify you meet level requirements
- Ensure equipment slot is not already occupied (or drag to replace)

### Performance Notes

Default configuration:
- **VSync:** Enabled (60 FPS cap)
- **Resolution:** 1920x1080 (configurable in Application settings)
- **OpenGL:** 3.3 Core Profile
- **Shadow Map:** 2048x2048 resolution
- **Particles:** 2000 max (weather), emitter-dependent (M2)

Optimization tips:
- Frustum culling automatically enabled
- Terrain streaming loads chunks as needed
- WMO distance culling at 160 units
- Async terrain loading prevents frame stalls

## Useful Resources

- **WoWDev Wiki:** https://wowdev.wiki/ - File formats and protocol documentation
- **TrinityCore:** https://github.com/TrinityCore/TrinityCore - Server reference implementation
- **MaNGOS:** https://github.com/cmangos/mangos-wotlk - Alternative server reference
- **StormLib:** https://github.com/ladislav-zezula/StormLib - MPQ library documentation
- **ImGui:** https://github.com/ocornut/imgui - UI framework

## Known Issues

- **Stormwind Mage Quarter**: Water overflows near Moonwell area (geometry constraint issue)
- **Particle emitters**: Some complex emitters may have minor visual glitches
- **Hair textures**: Occasional texture resolution on some race/gender combinations

See GitHub issues for full list and tracking.

## Getting Help

1. Check console output for error messages (use `LOG_DEBUG` builds for verbose output)
2. Consult WoWDev Wiki for protocol and format specifications
3. Review Architecture documentation for system design
4. Report bugs on GitHub issue tracker

---

**Enjoy playing WoW with a native C++ client!** 🎮
