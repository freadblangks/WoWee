# Build Instructions

This project builds as a native C++ client for WoW 3.3.5a in online mode.

## 1. Install Dependencies

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
  cmake build-essential pkg-config git \
  libsdl2-dev libglew-dev libglm-dev \
  libssl-dev zlib1g-dev \
  libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
  libstorm-dev
```

If `libstorm-dev` is unavailable in your distro repos, build StormLib from source:

```bash
cd /tmp
git clone https://github.com/ladislav-zezula/StormLib.git
cd StormLib
mkdir build && cd build
cmake ..
make -j"$(nproc)"
sudo make install
sudo ldconfig
```

### Fedora

```bash
sudo dnf install -y \
  cmake gcc-c++ make pkg-config git \
  SDL2-devel glew-devel glm-devel \
  openssl-devel zlib-devel \
  ffmpeg-devel \
  StormLib-devel
```

### Arch

```bash
sudo pacman -S --needed \
  cmake base-devel pkgconf git \
  sdl2 glew glm openssl zlib ffmpeg stormlib
```

## 2. Clone + Prepare

```bash
git clone https://github.com/Kelsidavis/WoWee.git
cd wowee
git clone https://github.com/ocornut/imgui.git extern/imgui
```

## 3. Configure + Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Binary output:

```text
build/bin/wowee
```

## 4. Provide WoW Data (Extract + Manifest)

Wowee loads assets from an extracted loose-file tree indexed by `manifest.json` (it does not read MPQs at runtime).

### Option A: Extract into `./Data/` (recommended)

Run:

```bash
# WotLK 3.3.5a example
./extract_assets.sh /path/to/WoW/Data wotlk
```

The output includes:

```text
Data/
  manifest.json
  interface/
  sound/
  world/
  expansions/
```

### Option B: Use an existing extracted data tree

Point wowee at your extracted `Data/` directory:

```bash
export WOW_DATA_PATH=/path/to/extracted/Data
```

## 5. Run

```bash
./build/bin/wowee
```

## 6. Local AzerothCore (Optional)

If you are using a local AzerothCore Docker stack, start it first and then connect from the client realm screen.

See:

- `docs/server-setup.md`

## Troubleshooting

### `StormLib` not found

Install distro package or build from source (section 1).

### `ImGui` missing

Ensure `extern/imgui` exists:

```bash
git clone https://github.com/ocornut/imgui.git extern/imgui
```

### Data not found at runtime

Verify `Data/manifest.json` exists (or re-run `./extract_assets.sh ...`), or set:

```bash
export WOW_DATA_PATH=/path/to/extracted/Data
```

### Clean rebuild

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```
