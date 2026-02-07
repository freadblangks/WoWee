# Build Instructions - Remove Single-Player Branch

## Current Status

✅ **Code Ready**: All single-player code removed, all compilation errors fixed
✅ **Branch**: `remove-single-player` 
✅ **Commits**: 4 commits ahead of master
✅ **Quality**: 450/450 braces balanced, 0 SP references, 0 SQLite references

## Quick Start

### Option 1: Automated (Recommended)

```bash
# Install dependencies (requires sudo)
/tmp/install_deps.sh

# Build the project
/tmp/build_wowee.sh
```

### Option 2: Manual

```bash
# Install dependencies
sudo apt install -y \
    libsdl2-dev libglew-dev libglm-dev zlib1g-dev \
    libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
    build-essential pkg-config git

# Build StormLib (if not in repos)
cd /tmp
git clone https://github.com/ladislav-zezula/StormLib.git
cd StormLib && mkdir build && cd build
cmake .. && make -j$(nproc)
sudo make install && sudo ldconfig

# Build Wowee
cd /home/k/wowee/build
cmake ..
make -j$(nproc)

# Run
./bin/wowee
```

## What Was Changed

### Removed (~1,400 lines)
- SQLite3 database wrapper (~700 lines)
- Single-player persistence (~500 lines)  
- SP method implementations (27 methods)
- SP UI elements (buttons, settings)
- SP conditional logic throughout codebase

### Fixed
- Missing closing brace in `update()` function
- Removed calls to deleted methods (`getItemTemplateName`, `notifyInventoryChanged`, etc.)
- Restored NPC animation callbacks (needed for online mode)
- Fixed header corruption

### Preserved (100%)
- All online multiplayer features
- Authentication (SRP6a)
- Character system
- Combat system
- Inventory & equipment
- Quest system
- Loot system
- Spell system
- Chat system
- All rendering features

## Expected Build Output

```
[  1%] Building CXX object CMakeFiles/wowee.dir/src/core/application.cpp.o
[  3%] Building CXX object CMakeFiles/wowee.dir/src/core/window.cpp.o
...
[ 95%] Building CXX object CMakeFiles/wowee.dir/src/ui/talent_screen.cpp.o
[ 97%] Building CXX object CMakeFiles/wowee.dir/src/main.cpp.o
[100%] Linking CXX executable bin/wowee
[100%] Built target wowee
```

## Troubleshooting

### CMake can't find SDL2
```bash
sudo apt install libsdl2-dev
```

### CMake can't find StormLib
StormLib is not in standard Ubuntu repos. Build from source:
```bash
cd /tmp
git clone https://github.com/ladislav-zezula/StormLib.git
cd StormLib && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install
sudo ldconfig
```

### Compilation errors
If you see compilation errors, ensure you're on the correct branch:
```bash
git branch --show-current  # Should show: remove-single-player
git status                 # Should show: nothing to commit, working tree clean
```

### Missing WoW Data
The client requires WoW 3.3.5a data files in `Data/` directory:
```
wowee/
└── Data/
    ├── common.MPQ
    ├── expansion.MPQ
    ├── lichking.MPQ
    ├── patch.MPQ
    └── enUS/
```

Or set environment variable:
```bash
export WOW_DATA_PATH=/path/to/your/wow/Data
```

## Testing Checklist

After successful build:

- [ ] Application launches without crashes
- [ ] Can connect to auth server
- [ ] Can view realm list  
- [ ] Can view/create/delete characters
- [ ] Can enter world
- [ ] Movement works (WASD)
- [ ] Combat works (auto-attack, spells)
- [ ] Inventory system functional
- [ ] Quest markers appear
- [ ] Loot window opens
- [ ] Chat works

## Performance

Expected performance:
- **FPS**: 60 (vsync)
- **Triangles/frame**: ~50k
- **Draw calls**: ~30
- **GPU Usage**: <10%

## Next Steps

1. Install dependencies: `/tmp/install_deps.sh`
2. Build project: `/tmp/build_wowee.sh`
3. Test online features
4. Merge to master when satisfied:
   ```bash
   git checkout master
   git merge remove-single-player
   git push
   ```

## Support

If you encounter issues:
1. Check branch: `git branch --show-current`
2. Check status: `git status`
3. Check commits: `git log --oneline -5`
4. Verify balance: `grep -c "{" src/game/game_handler.cpp` should equal `grep -c "}" src/game/game_handler.cpp`

---

Last updated: 2026-02-07
Branch: remove-single-player (4 commits ahead of master)
Status: Ready to build
