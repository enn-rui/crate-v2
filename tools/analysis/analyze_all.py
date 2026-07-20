#!/usr/bin/env python3
"""Run Crate's full local analysis pipeline in order."""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODEL_CACHE_NAME = "models--OpenMuQ--MuQ-MuLan-large"
STEPS = [
    ("BPM / key / energy", "analyze.py", {"limit", "rebuild"}),
    ("MuQ-MuLan embeddings", "embed_muq.py", {"limit", "rebuild"}),
    ("sonic clusters (full 512-d)", "cluster.py", set()),
    ("PaCMAP map (tracks, 2D+3D)", "umap_music.py", set()),
    ("UMAP map (artists)", "umap_artists.py", set()),
    ("colored waveforms", "waveform.py", {"limit", "rebuild"}),
]


def model_cache_dir() -> Path:
    hf_home = os.environ.get("HF_HOME")
    if hf_home:
        return Path(hf_home).expanduser() / "hub" / MODEL_CACHE_NAME
    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    cache_root = Path(xdg_cache).expanduser() if xdg_cache else Path.home() / ".cache"
    return cache_root / "huggingface" / "hub" / MODEL_CACHE_NAME


def check_environment() -> int:
    try:
        import torch
    except (ImportError, OSError) as error:
        print(f"analysis not ready: torch could not be imported ({error})", file=sys.stderr)
        print("Run setup.ps1 first, then run: .\\analyze.ps1 -Root \"C:\\path\\to\\music\"",
              file=sys.stderr)
        return 1

    cache = model_cache_dir()
    snapshots = cache / "snapshots"
    weight_suffixes = {".bin", ".safetensors", ".pt", ".pth"}
    model_present = snapshots.is_dir() and any(
        p.is_file() and p.suffix.lower() in weight_suffixes for p in snapshots.rglob("*"))
    device = "CUDA" if torch.cuda.is_available() else "CPU"
    if model_present:
        print(f"ready: torch {torch.__version__} imports ({device}); MuQ-MuLan cache found")
    else:
        print(f"ready: torch {torch.__version__} imports ({device})")
        print("MuQ-MuLan is not cached; first run will download approximately 2 GB.")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Run Crate's local analysis pipeline")
    ap.add_argument("--root", help="required music library root")
    ap.add_argument("--out", help="sidecar directory (default: <root>/.crate)")
    ap.add_argument("--limit", type=int, default=0, help="cap tracks per supported step")
    ap.add_argument("--rebuild", action="store_true", help="reanalyze everything")
    ap.add_argument("--skip", default="", help="comma-separated step scripts to skip")
    ap.add_argument("--check", action="store_true", help="check torch and model-cache readiness")
    args = ap.parse_args(argv)
    if args.check:
        return check_environment()
    if not args.root:
        ap.error('Music root is required. Run: python analyze_all.py --root "C:\\path\\to\\music"')

    skip = {s.strip() for s in args.skip.split(",") if s.strip()}
    frozen = getattr(sys, "frozen", False)
    for label, script, supports in STEPS:
        if script in skip:
            print(f"\n----- skipping {script} -----", flush=True)
            continue
        cmd = ([sys.executable, "--step", Path(script).stem] if frozen else
               [sys.executable, str(HERE / script)])
        cmd += ["--root", args.root]
        if args.out:
            cmd += ["--out", args.out]
        if args.limit and "limit" in supports:
            cmd += ["--limit", str(args.limit)]
        if args.rebuild and "rebuild" in supports:
            cmd += ["--rebuild"]
        print(f"\n===== {label}  ({script}) =====", flush=True)
        started = time.time()
        result = subprocess.run(cmd)
        if result.returncode != 0:
            print(f"!! {script} failed (exit {result.returncode}); stopping.", flush=True)
            return result.returncode
        print(f"----- {label} done in {time.time() - started:.0f}s -----", flush=True)

    print("\n=== analysis complete; refresh MAP (or restart Mixxx) ===", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
