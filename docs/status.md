# Project Status

**Last updated**: 2026-02-15

## What This Repo Is

Wowee is a native C++ World of Warcraft client experiment focused on connecting to real emulator servers (online/multiplayer) with a custom renderer and asset pipeline.

## Current Code State

Implemented (working in normal development use):

- Auth flow: SRP6a auth + realm list + world connect with header encryption
- Rendering: terrain, WMO/M2 rendering, water, sky system, particles, minimap/world map, loading video playback
- Core gameplay plumbing: movement, targeting, action bar basics, inventory/equipment visuals, chat (tabs/channels, emotes, item links)
- Multi-expansion direction: Classic/TBC/WotLK protocol variance handling exists and is being extended (`src/game/packet_parsers_classic.cpp`, `src/game/packet_parsers_tbc.cpp`)

In progress / incomplete (known gaps):

- Quests: some quest UI/markers exist, but parts of quest log parsing are still TODOs
- Transports: functional support exists, but some spline parsing/edge cases are still TODOs
- Audio: broad coverage for events/music/UI exists, but 3D positional audio is not implemented yet
- Warden: crypto + module plumbing are in place; full module execution and server-specific compatibility are still in progress

## Near-Term Direction

- Keep tightening packet parsing across server variants (especially Classic/Turtle and TBC)
- Keep improving visual correctness for characters/equipment and M2/WMO edge cases
- Progress Warden module execution path (emulation via Unicorn when available)

## Where To Look

- Entry point: `src/main.cpp`, `src/core/application.cpp`
- Networking/auth: `src/auth/`, `src/network/`, `src/game/game_handler.cpp`
- Rendering: `src/rendering/`
- Assets/extraction: `extract_assets.sh`, `tools/asset_extract/`, `src/pipeline/asset_manager.cpp`

