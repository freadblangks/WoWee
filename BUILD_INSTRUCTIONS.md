# WoWee Build Instructions

This document provides platform-specific build instructions for WoWee.

---

## 🐧 Linux (Ubuntu / Debian)

### Install Dependencies

```bash
sudo apt update
sudo apt install -y   build-essential cmake pkg-config git   libsdl2-dev libglew-dev libglm-dev   libssl-dev zlib1g-dev   libavcodec-dev libavformat-dev libavutil-dev libswscale-dev   libunicorn-dev   libstorm-dev
```

---

## 🐧 Linux (Arch)

### Install Dependencies

```bash
sudo pacman -S --needed   base-devel cmake pkgconf git   sdl2 glew glm   openssl zlib   ffmpeg   unicorn   stormlib
```

---

## 🐧 Linux (All Distros)

### Clone Repository

Always clone with submodules:

```bash
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git
cd WoWee
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

---

## 🪟 Windows (Visual Studio 2022)

### Install

- Visual Studio 2022
- Desktop development with C++
- CMake tools for Windows

### Clone

```powershell
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git
cd WoWee
```

### Build

Open the folder in Visual Studio (it will detect CMake automatically)  
or build from Developer PowerShell:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## ⚠️ Notes

- Case matters on Linux (`WoWee` not `wowee`).
- Always use `--recurse-submodules` when cloning.
- If you encounter missing headers for ImGui, run:
  ```bash
  git submodule update --init --recursive
  ```
