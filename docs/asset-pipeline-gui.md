# Asset Pipeline GUI

WoWee includes a Python GUI for extraction and texture-pack management:

```bash
python3 tools/asset_pipeline_gui.py
```

## Supported Platforms

- Linux
- macOS
- Windows

The app uses Python's built-in `tkinter` module. If `tkinter` is missing, install the platform package:

- Linux (Debian/Ubuntu): `sudo apt install python3-tk`
- Fedora: `sudo dnf install python3-tkinter`
- Arch: `sudo pacman -S tk`
- macOS: use the official Python.org installer (includes Tk)
- Windows: use the official Python installer and enable Tcl/Tk support

## What It Does

- Runs `asset_extract` (or `extract_assets.sh` fallback on non-Windows)
- Saves extraction config in `asset_pipeline/state.json`
- Installs texture packs from ZIP or folders
- Lets users activate/deactivate packs and reorder active pack priority
- Rebuilds `Data/override` from active pack order
- Shows current data state (`manifest.json`, entry count, override file count, last runs)

## Pack Format

Supported pack layouts:

1. `PackName/Data/...`
2. `PackName/data/...`
3. `PackName/...` where top folders include game folders (`Interface`, `World`, `Character`, `Textures`, `Sound`)

When multiple active packs contain the same file path, **later packs in active order win**.

## State Files and Folders

- Pipeline state: `asset_pipeline/state.json`
- Installed packs: `asset_pipeline/packs/<pack-id>/`
- Active merged override output: `<Output Data>/override/`

## Typical Workflow

1. Open the GUI.
2. Set WoW MPQ Data source and output Data path.
3. Run extraction.
4. Install texture packs.
5. Activate and order packs.
6. Click **Rebuild Override**.
7. Launch wowee with `WOW_DATA_PATH` pointing at your output Data path if needed.
