#!/usr/bin/env python3
"""WoWee Asset Pipeline GUI.

Cross-platform Tkinter app for running asset extraction and managing texture packs
that are merged into Data/override in deterministic order.
"""

from __future__ import annotations

import json
import platform
import queue
import shutil
import subprocess
import threading
import time
import zipfile
from dataclasses import asdict, dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText


ROOT_DIR = Path(__file__).resolve().parents[1]
PIPELINE_DIR = ROOT_DIR / "asset_pipeline"
STATE_FILE = PIPELINE_DIR / "state.json"


@dataclass
class PackInfo:
    pack_id: str
    name: str
    source: str
    installed_dir: str
    installed_at: str
    file_count: int = 0


@dataclass
class AppState:
    wow_data_dir: str = ""
    output_data_dir: str = str(ROOT_DIR / "Data")
    extractor_path: str = ""
    expansion: str = "auto"
    locale: str = "auto"
    skip_dbc: bool = False
    dbc_csv: bool = False
    verify: bool = False
    verbose: bool = False
    threads: int = 0
    packs: list[PackInfo] = field(default_factory=list)
    active_pack_ids: list[str] = field(default_factory=list)
    last_extract_at: str = ""
    last_extract_ok: bool = False
    last_extract_command: str = ""
    last_override_build_at: str = ""


class PipelineManager:
    def __init__(self) -> None:
        PIPELINE_DIR.mkdir(parents=True, exist_ok=True)
        (PIPELINE_DIR / "packs").mkdir(parents=True, exist_ok=True)
        self.state = self._load_state()

    def _default_state(self) -> AppState:
        return AppState()

    def _load_state(self) -> AppState:
        if not STATE_FILE.exists():
            return self._default_state()
        try:
            doc = json.loads(STATE_FILE.read_text(encoding="utf-8"))
            packs = [PackInfo(**item) for item in doc.get("packs", [])]
            doc["packs"] = packs
            state = AppState(**doc)
            return state
        except (OSError, ValueError, TypeError):
            return self._default_state()

    def save_state(self) -> None:
        serializable = asdict(self.state)
        STATE_FILE.write_text(json.dumps(serializable, indent=2), encoding="utf-8")

    def now_str(self) -> str:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    def _normalize_id(self, name: str) -> str:
        raw = "".join(ch.lower() if ch.isalnum() else "-" for ch in name).strip("-")
        base = raw or "pack"
        return f"{base}-{int(time.time())}"

    def _pack_dir(self, pack_id: str) -> Path:
        return PIPELINE_DIR / "packs" / pack_id

    def _looks_like_data_root(self, path: Path) -> bool:
        markers = {"interface", "world", "character", "textures", "sound"}
        names = {p.name.lower() for p in path.iterdir() if p.is_dir()} if path.is_dir() else set()
        return bool(markers.intersection(names))

    def find_data_root(self, pack_path: Path) -> Path:
        direct_data = pack_path / "Data"
        if direct_data.is_dir():
            return direct_data

        lower_data = pack_path / "data"
        if lower_data.is_dir():
            return lower_data

        if self._looks_like_data_root(pack_path):
            return pack_path

        # Common zip layout: one wrapper directory.
        children = [p for p in pack_path.iterdir() if p.is_dir()] if pack_path.is_dir() else []
        if len(children) == 1:
            child = children[0]
            child_data = child / "Data"
            if child_data.is_dir():
                return child_data
            if self._looks_like_data_root(child):
                return child

        return pack_path

    def _count_files(self, root: Path) -> int:
        if not root.exists():
            return 0
        return sum(1 for p in root.rglob("*") if p.is_file())

    def install_pack_from_zip(self, zip_path: Path) -> PackInfo:
        pack_name = zip_path.stem
        pack_id = self._normalize_id(pack_name)
        target = self._pack_dir(pack_id)
        target.mkdir(parents=True, exist_ok=False)

        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(target)

        data_root = self.find_data_root(target)
        info = PackInfo(
            pack_id=pack_id,
            name=pack_name,
            source=str(zip_path),
            installed_dir=str(target),
            installed_at=self.now_str(),
            file_count=self._count_files(data_root),
        )
        self.state.packs.append(info)
        self.save_state()
        return info

    def install_pack_from_folder(self, folder_path: Path) -> PackInfo:
        pack_name = folder_path.name
        pack_id = self._normalize_id(pack_name)
        target = self._pack_dir(pack_id)
        shutil.copytree(folder_path, target)

        data_root = self.find_data_root(target)
        info = PackInfo(
            pack_id=pack_id,
            name=pack_name,
            source=str(folder_path),
            installed_dir=str(target),
            installed_at=self.now_str(),
            file_count=self._count_files(data_root),
        )
        self.state.packs.append(info)
        self.save_state()
        return info

    def uninstall_pack(self, pack_id: str) -> None:
        self.state.packs = [p for p in self.state.packs if p.pack_id != pack_id]
        self.state.active_pack_ids = [pid for pid in self.state.active_pack_ids if pid != pack_id]
        target = self._pack_dir(pack_id)
        if target.exists():
            shutil.rmtree(target)
        self.save_state()

    def set_pack_active(self, pack_id: str, active: bool) -> None:
        if active:
            if pack_id not in self.state.active_pack_ids:
                self.state.active_pack_ids.append(pack_id)
        else:
            self.state.active_pack_ids = [pid for pid in self.state.active_pack_ids if pid != pack_id]
        self.save_state()

    def move_active_pack(self, pack_id: str, delta: int) -> None:
        ids = self.state.active_pack_ids
        if pack_id not in ids:
            return
        idx = ids.index(pack_id)
        nidx = idx + delta
        if nidx < 0 or nidx >= len(ids):
            return
        ids[idx], ids[nidx] = ids[nidx], ids[idx]
        self.state.active_pack_ids = ids
        self.save_state()

    def rebuild_override(self) -> dict[str, int]:
        out_dir = Path(self.state.output_data_dir)
        override_dir = out_dir / "override"
        override_dir.mkdir(parents=True, exist_ok=True)

        if override_dir.exists():
            shutil.rmtree(override_dir)
        override_dir.mkdir(parents=True, exist_ok=True)

        copied = 0
        replaced = 0

        active_map = {p.pack_id: p for p in self.state.packs}
        for pack_id in self.state.active_pack_ids:
            info = active_map.get(pack_id)
            if info is None:
                continue
            pack_dir = Path(info.installed_dir)
            if not pack_dir.exists():
                continue

            data_root = self.find_data_root(pack_dir)
            for source in data_root.rglob("*"):
                if not source.is_file():
                    continue
                rel = source.relative_to(data_root)
                target = override_dir / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists():
                    replaced += 1
                shutil.copy2(source, target)
                copied += 1

        self.state.last_override_build_at = self.now_str()
        self.save_state()
        return {"copied": copied, "replaced": replaced}

    def _resolve_extractor(self) -> list[str] | None:
        configured = self.state.extractor_path.strip()
        if configured:
            path = Path(configured)
            if path.exists() and path.is_file():
                return [str(path)]

        ext = ".exe" if platform.system().lower().startswith("win") else ""
        for candidate in [
            ROOT_DIR / "build" / "bin" / f"asset_extract{ext}",
            ROOT_DIR / "bin" / f"asset_extract{ext}",
        ]:
            if candidate.exists():
                return [str(candidate)]

        if platform.system().lower().startswith("win"):
            return None

        shell_script = ROOT_DIR / "extract_assets.sh"
        if shell_script.exists():
            return ["bash", str(shell_script)]

        return None

    def build_extract_command(self) -> list[str]:
        mpq_dir = self.state.wow_data_dir.strip()
        output_dir = self.state.output_data_dir.strip()
        if not mpq_dir or not output_dir:
            raise ValueError("Both WoW Data directory and output directory are required.")

        extractor = self._resolve_extractor()
        if extractor is None:
            raise ValueError(
                "No extractor found. Build asset_extract first or set the extractor path in Configuration."
            )

        if extractor[0].endswith("extract_assets.sh") or extractor[-1].endswith("extract_assets.sh"):
            cmd = [*extractor, mpq_dir]
            if self.state.expansion and self.state.expansion != "auto":
                cmd.append(self.state.expansion)
            return cmd

        cmd = [*extractor, "--mpq-dir", mpq_dir, "--output", output_dir]
        if self.state.expansion and self.state.expansion != "auto":
            cmd.extend(["--expansion", self.state.expansion])
        if self.state.locale and self.state.locale != "auto":
            cmd.extend(["--locale", self.state.locale])
        if self.state.skip_dbc:
            cmd.append("--skip-dbc")
        if self.state.dbc_csv:
            cmd.append("--dbc-csv")
        if self.state.verify:
            cmd.append("--verify")
        if self.state.verbose:
            cmd.append("--verbose")
        if self.state.threads > 0:
            cmd.extend(["--threads", str(self.state.threads)])
        return cmd

    def summarize_state(self) -> dict[str, Any]:
        output_dir = Path(self.state.output_data_dir)
        manifest_path = output_dir / "manifest.json"
        override_dir = output_dir / "override"

        summary: dict[str, Any] = {
            "output_dir": str(output_dir),
            "output_exists": output_dir.exists(),
            "manifest_exists": manifest_path.exists(),
            "manifest_entries": 0,
            "override_exists": override_dir.exists(),
            "override_files": self._count_files(override_dir),
            "packs_installed": len(self.state.packs),
            "packs_active": len(self.state.active_pack_ids),
            "last_extract_at": self.state.last_extract_at or "never",
            "last_extract_ok": self.state.last_extract_ok,
            "last_override_build_at": self.state.last_override_build_at or "never",
        }

        if manifest_path.exists():
            try:
                doc = json.loads(manifest_path.read_text(encoding="utf-8"))
                entries = doc.get("entries", {})
                if isinstance(entries, dict):
                    summary["manifest_entries"] = len(entries)
            except (OSError, ValueError, TypeError):
                summary["manifest_entries"] = -1

        return summary


class AssetPipelineGUI:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.manager = PipelineManager()

        self.log_queue: queue.Queue[str] = queue.Queue()
        self.proc_thread: threading.Thread | None = None
        self.proc_running = False

        self.root.title("WoWee Asset Pipeline")
        self.root.geometry("1120x760")

        self.status_var = tk.StringVar(value="Ready")
        self._build_ui()
        self._load_vars_from_state()
        self.refresh_pack_list()
        self.refresh_state_view()
        self.root.after(120, self._poll_logs)

    def _build_ui(self) -> None:
        top = ttk.Frame(self.root, padding=10)
        top.pack(fill="both", expand=True)

        status = ttk.Label(top, textvariable=self.status_var, anchor="w")
        status.pack(fill="x", pady=(0, 8))

        self.notebook = ttk.Notebook(top)
        self.notebook.pack(fill="both", expand=True)

        self.cfg_tab = ttk.Frame(self.notebook, padding=10)
        self.packs_tab = ttk.Frame(self.notebook, padding=10)
        self.state_tab = ttk.Frame(self.notebook, padding=10)
        self.logs_tab = ttk.Frame(self.notebook, padding=10)

        self.notebook.add(self.cfg_tab, text="Configuration")
        self.notebook.add(self.packs_tab, text="Texture Packs")
        self.notebook.add(self.state_tab, text="Current State")
        self.notebook.add(self.logs_tab, text="Logs")

        self._build_config_tab()
        self._build_packs_tab()
        self._build_state_tab()
        self._build_logs_tab()

    def _build_config_tab(self) -> None:
        self.var_wow_data = tk.StringVar()
        self.var_output_data = tk.StringVar()
        self.var_extractor = tk.StringVar()
        self.var_expansion = tk.StringVar(value="auto")
        self.var_locale = tk.StringVar(value="auto")
        self.var_skip_dbc = tk.BooleanVar(value=False)
        self.var_dbc_csv = tk.BooleanVar(value=False)
        self.var_verify = tk.BooleanVar(value=False)
        self.var_verbose = tk.BooleanVar(value=False)
        self.var_threads = tk.IntVar(value=0)

        frame = self.cfg_tab

        self._path_row(frame, 0, "WoW Data (MPQ source)", self.var_wow_data, self._pick_wow_data_dir)
        self._path_row(frame, 1, "Output Data directory", self.var_output_data, self._pick_output_dir)
        self._path_row(frame, 2, "Extractor binary/script (optional)", self.var_extractor, self._pick_extractor)

        ttk.Label(frame, text="Expansion").grid(row=3, column=0, sticky="w", pady=6)
        exp_combo = ttk.Combobox(
            frame,
            textvariable=self.var_expansion,
            values=["auto", "classic", "turtle", "tbc", "wotlk"],
            state="readonly",
            width=18,
        )
        exp_combo.grid(row=3, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Locale").grid(row=3, column=2, sticky="w", pady=6)
        loc_combo = ttk.Combobox(
            frame,
            textvariable=self.var_locale,
            values=["auto", "enUS", "enGB", "deDE", "frFR", "esES", "esMX", "ruRU", "koKR", "zhCN", "zhTW"],
            state="normal",
            width=12,
        )
        loc_combo.grid(row=3, column=3, sticky="w", pady=6)

        ttk.Label(frame, text="Threads (0 = auto)").grid(row=4, column=0, sticky="w", pady=6)
        ttk.Spinbox(frame, from_=0, to=256, textvariable=self.var_threads, width=8).grid(
            row=4, column=1, sticky="w", pady=6
        )

        opts = ttk.Frame(frame)
        opts.grid(row=5, column=0, columnspan=4, sticky="w", pady=6)
        ttk.Checkbutton(opts, text="Skip DBC extraction", variable=self.var_skip_dbc).pack(side="left", padx=(0, 12))
        ttk.Checkbutton(opts, text="Generate DBC CSV", variable=self.var_dbc_csv).pack(side="left", padx=(0, 12))
        ttk.Checkbutton(opts, text="Verify CRC", variable=self.var_verify).pack(side="left", padx=(0, 12))
        ttk.Checkbutton(opts, text="Verbose output", variable=self.var_verbose).pack(side="left", padx=(0, 12))

        buttons = ttk.Frame(frame)
        buttons.grid(row=6, column=0, columnspan=4, sticky="w", pady=12)
        ttk.Button(buttons, text="Save Configuration", command=self.save_config).pack(side="left", padx=(0, 8))
        ttk.Button(buttons, text="Run Extraction", command=self.run_extraction).pack(side="left", padx=(0, 8))
        ttk.Button(buttons, text="Refresh State", command=self.refresh_state_view).pack(side="left")

        tip = (
            "Texture packs are merged into <Output Data>/override in active order. "
            "Later packs override earlier packs file-by-file."
        )
        ttk.Label(frame, text=tip, foreground="#444").grid(row=7, column=0, columnspan=4, sticky="w", pady=(8, 0))

        frame.columnconfigure(1, weight=1)

    def _build_packs_tab(self) -> None:
        left = ttk.Frame(self.packs_tab)
        left.pack(side="left", fill="both", expand=True)

        right = ttk.Frame(self.packs_tab)
        right.pack(side="right", fill="y", padx=(12, 0))

        self.pack_list = tk.Listbox(left, height=22)
        self.pack_list.pack(fill="both", expand=True)
        self.pack_list.bind("<<ListboxSelect>>", lambda _evt: self._refresh_pack_detail())

        self.pack_detail = ScrolledText(left, height=10, wrap="word", state="disabled")
        self.pack_detail.pack(fill="both", expand=False, pady=(10, 0))

        ttk.Button(right, text="Install ZIP", width=22, command=self.install_zip).pack(pady=4)
        ttk.Button(right, text="Install Folder", width=22, command=self.install_folder).pack(pady=4)
        ttk.Separator(right, orient="horizontal").pack(fill="x", pady=8)
        ttk.Button(right, text="Activate", width=22, command=self.activate_selected_pack).pack(pady=4)
        ttk.Button(right, text="Deactivate", width=22, command=self.deactivate_selected_pack).pack(pady=4)
        ttk.Button(right, text="Move Up", width=22, command=lambda: self.move_selected_pack(-1)).pack(pady=4)
        ttk.Button(right, text="Move Down", width=22, command=lambda: self.move_selected_pack(1)).pack(pady=4)
        ttk.Separator(right, orient="horizontal").pack(fill="x", pady=8)
        ttk.Button(right, text="Rebuild Override", width=22, command=self.rebuild_override).pack(pady=4)
        ttk.Button(right, text="Uninstall", width=22, command=self.uninstall_selected_pack).pack(pady=4)

    def _build_state_tab(self) -> None:
        actions = ttk.Frame(self.state_tab)
        actions.pack(fill="x")
        ttk.Button(actions, text="Refresh", command=self.refresh_state_view).pack(side="left")

        self.state_text = ScrolledText(self.state_tab, wrap="word", state="disabled")
        self.state_text.pack(fill="both", expand=True, pady=(10, 0))

    def _build_logs_tab(self) -> None:
        actions = ttk.Frame(self.logs_tab)
        actions.pack(fill="x")
        ttk.Button(actions, text="Clear Logs", command=self.clear_logs).pack(side="left")

        self.log_text = ScrolledText(self.logs_tab, wrap="none", state="disabled")
        self.log_text.pack(fill="both", expand=True, pady=(10, 0))

    def _path_row(self, frame: ttk.Frame, row: int, label: str, variable: tk.StringVar, browse_cmd) -> None:
        ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", pady=6)
        ttk.Entry(frame, textvariable=variable).grid(row=row, column=1, columnspan=2, sticky="ew", pady=6)
        ttk.Button(frame, text="Browse", command=browse_cmd).grid(row=row, column=3, sticky="e", pady=6)

    def _pick_wow_data_dir(self) -> None:
        picked = filedialog.askdirectory(title="Select WoW Data directory")
        if picked:
            self.var_wow_data.set(picked)

    def _pick_output_dir(self) -> None:
        picked = filedialog.askdirectory(title="Select output Data directory")
        if picked:
            self.var_output_data.set(picked)

    def _pick_extractor(self) -> None:
        picked = filedialog.askopenfilename(title="Select extractor binary or script")
        if picked:
            self.var_extractor.set(picked)

    def _load_vars_from_state(self) -> None:
        st = self.manager.state
        self.var_wow_data.set(st.wow_data_dir)
        self.var_output_data.set(st.output_data_dir)
        self.var_extractor.set(st.extractor_path)
        self.var_expansion.set(st.expansion)
        self.var_locale.set(st.locale)
        self.var_skip_dbc.set(st.skip_dbc)
        self.var_dbc_csv.set(st.dbc_csv)
        self.var_verify.set(st.verify)
        self.var_verbose.set(st.verbose)
        self.var_threads.set(st.threads)

    def save_config(self) -> None:
        st = self.manager.state
        st.wow_data_dir = self.var_wow_data.get().strip()
        st.output_data_dir = self.var_output_data.get().strip()
        st.extractor_path = self.var_extractor.get().strip()
        st.expansion = self.var_expansion.get().strip() or "auto"
        st.locale = self.var_locale.get().strip() or "auto"
        st.skip_dbc = bool(self.var_skip_dbc.get())
        st.dbc_csv = bool(self.var_dbc_csv.get())
        st.verify = bool(self.var_verify.get())
        st.verbose = bool(self.var_verbose.get())
        st.threads = int(self.var_threads.get())
        self.manager.save_state()
        self.status_var.set("Configuration saved")

    def _selected_pack(self) -> PackInfo | None:
        sel = self.pack_list.curselection()
        if not sel:
            return None
        idx = int(sel[0])
        if idx < 0 or idx >= len(self.manager.state.packs):
            return None
        return self.manager.state.packs[idx]

    def refresh_pack_list(self) -> None:
        active = self.manager.state.active_pack_ids
        self.pack_list.delete(0, tk.END)
        for pack in self.manager.state.packs:
            marker = ""
            if pack.pack_id in active:
                marker = f"[active #{active.index(pack.pack_id) + 1}] "
            self.pack_list.insert(tk.END, f"{marker}{pack.name}")
        self._refresh_pack_detail()

    def _refresh_pack_detail(self) -> None:
        pack = self._selected_pack()
        self.pack_detail.configure(state="normal")
        self.pack_detail.delete("1.0", tk.END)
        if pack is None:
            self.pack_detail.insert(tk.END, "Select a texture pack to see details.")
            self.pack_detail.configure(state="disabled")
            return

        active = "yes" if pack.pack_id in self.manager.state.active_pack_ids else "no"
        order = "-"
        if pack.pack_id in self.manager.state.active_pack_ids:
            order = str(self.manager.state.active_pack_ids.index(pack.pack_id) + 1)
        lines = [
            f"Name: {pack.name}",
            f"Active: {active}",
            f"Order: {order}",
            f"Files: {pack.file_count}",
            f"Installed at: {pack.installed_at}",
            f"Installed dir: {pack.installed_dir}",
            f"Source: {pack.source}",
        ]
        self.pack_detail.insert(tk.END, "\n".join(lines))
        self.pack_detail.configure(state="disabled")

    def install_zip(self) -> None:
        zip_path = filedialog.askopenfilename(
            title="Choose texture pack ZIP",
            filetypes=[("ZIP archives", "*.zip"), ("All files", "*.*")],
        )
        if not zip_path:
            return
        try:
            info = self.manager.install_pack_from_zip(Path(zip_path))
        except Exception as exc:  # pylint: disable=broad-except
            messagebox.showerror("Install failed", str(exc))
            return

        self.refresh_pack_list()
        self.refresh_state_view()
        self.status_var.set(f"Installed pack: {info.name}")

    def install_folder(self) -> None:
        folder = filedialog.askdirectory(title="Choose texture pack folder")
        if not folder:
            return
        try:
            info = self.manager.install_pack_from_folder(Path(folder))
        except Exception as exc:  # pylint: disable=broad-except
            messagebox.showerror("Install failed", str(exc))
            return

        self.refresh_pack_list()
        self.refresh_state_view()
        self.status_var.set(f"Installed pack: {info.name}")

    def activate_selected_pack(self) -> None:
        pack = self._selected_pack()
        if pack is None:
            return
        self.manager.set_pack_active(pack.pack_id, True)
        self.refresh_pack_list()
        self.refresh_state_view()
        self.status_var.set(f"Activated pack: {pack.name}")

    def deactivate_selected_pack(self) -> None:
        pack = self._selected_pack()
        if pack is None:
            return
        self.manager.set_pack_active(pack.pack_id, False)
        self.refresh_pack_list()
        self.refresh_state_view()
        self.status_var.set(f"Deactivated pack: {pack.name}")

    def move_selected_pack(self, delta: int) -> None:
        pack = self._selected_pack()
        if pack is None:
            return
        self.manager.move_active_pack(pack.pack_id, delta)
        self.refresh_pack_list()
        self.refresh_state_view()
        self.status_var.set(f"Reordered active pack: {pack.name}")

    def uninstall_selected_pack(self) -> None:
        pack = self._selected_pack()
        if pack is None:
            return
        ok = messagebox.askyesno("Confirm uninstall", f"Uninstall texture pack '{pack.name}'?")
        if not ok:
            return
        self.manager.uninstall_pack(pack.pack_id)
        self.refresh_pack_list()
        self.refresh_state_view()
        self.status_var.set(f"Uninstalled pack: {pack.name}")

    def rebuild_override(self) -> None:
        try:
            report = self.manager.rebuild_override()
        except Exception as exc:  # pylint: disable=broad-except
            messagebox.showerror("Override rebuild failed", str(exc))
            return
        self.refresh_state_view()
        self.status_var.set(
            f"Override rebuilt: {report['copied']} files copied, {report['replaced']} replaced"
        )
        self._append_log(
            f"[{self.manager.now_str()}] Override rebuild complete: {report['copied']} copied, {report['replaced']} replaced"
        )

    def clear_logs(self) -> None:
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state="disabled")

    def _append_log(self, line: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert(tk.END, line + "\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state="disabled")

    def _poll_logs(self) -> None:
        while True:
            try:
                line = self.log_queue.get_nowait()
            except queue.Empty:
                break
            self._append_log(line)
        self.root.after(120, self._poll_logs)

    def run_extraction(self) -> None:
        if self.proc_running:
            messagebox.showinfo("Extraction running", "An extraction is already running.")
            return

        self.save_config()

        try:
            cmd = self.manager.build_extract_command()
        except ValueError as exc:
            messagebox.showerror("Cannot run extraction", str(exc))
            return

        def worker() -> None:
            self.proc_running = True
            started = self.manager.now_str()
            self.log_queue.put(f"[{started}] Running: {' '.join(cmd)}")
            self.root.after(0, lambda: self.status_var.set("Extraction running..."))

            ok = False
            try:
                process = subprocess.Popen(
                    cmd,
                    cwd=str(ROOT_DIR),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )
                assert process.stdout is not None
                for line in process.stdout:
                    self.log_queue.put(line.rstrip())
                rc = process.wait()
                ok = rc == 0
                if not ok:
                    self.log_queue.put(f"Extractor exited with status {rc}")
            except Exception as exc:  # pylint: disable=broad-except
                self.log_queue.put(f"Extraction error: {exc}")
            finally:
                self.manager.state.last_extract_at = self.manager.now_str()
                self.manager.state.last_extract_ok = ok
                self.manager.state.last_extract_command = " ".join(cmd)
                self.manager.save_state()
                self.proc_running = False
                self.root.after(0, self.refresh_state_view)
                self.root.after(
                    0, lambda: self.status_var.set("Extraction complete" if ok else "Extraction failed")
                )

        self.proc_thread = threading.Thread(target=worker, daemon=True)
        self.proc_thread.start()

    def refresh_state_view(self) -> None:
        summary = self.manager.summarize_state()

        active_names = []
        pack_map = {p.pack_id: p.name for p in self.manager.state.packs}
        for pid in self.manager.state.active_pack_ids:
            active_names.append(pack_map.get(pid, f"<missing {pid}>"))

        lines = [
            "WoWee Asset Pipeline State",
            "",
            f"Output directory: {summary['output_dir']}",
            f"Output exists: {summary['output_exists']}",
            f"manifest.json present: {summary['manifest_exists']}",
            f"Manifest entries: {summary['manifest_entries']}",
            "",
            f"Override folder present: {summary['override_exists']}",
            f"Override file count: {summary['override_files']}",
            f"Last override build: {summary['last_override_build_at']}",
            "",
            f"Installed texture packs: {summary['packs_installed']}",
            f"Active texture packs: {summary['packs_active']}",
            "Active order:",
        ]
        if active_names:
            for i, name in enumerate(active_names, start=1):
                lines.append(f"  {i}. {name}")
        else:
            lines.append("  (none)")

        lines.extend(
            [
                "",
                f"Last extraction time: {summary['last_extract_at']}",
                f"Last extraction success: {summary['last_extract_ok']}",
                f"Last extraction command: {self.manager.state.last_extract_command or '(none)'}",
                "",
                "Pipeline files:",
                f"  State file: {STATE_FILE}",
                f"  Packs dir:  {PIPELINE_DIR / 'packs'}",
            ]
        )

        self.state_text.configure(state="normal")
        self.state_text.delete("1.0", tk.END)
        self.state_text.insert(tk.END, "\n".join(lines))
        self.state_text.configure(state="disabled")


def main() -> None:
    root = tk.Tk()
    AssetPipelineGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
