# Crate — quickstart

**ALPHA SOFTWARE.** Crate is a young project under heavy active development.
Expect rough edges, breaking changes between builds, and features that assume a
particular setup. Back up anything you care about before pointing Crate at it.

Crate is a DJ app built on the Mixxx engine: a rekordbox-style layout, a sonic
galaxy map of your library (tracks that sound alike sit near each other), smart
crates, and full DJ controller support. This guide takes you from zero to mixing
off the map.

## 1. Install

Download the latest `.msi` from this repo's **Releases** page and run it.

Honest caveat: the installer is not code-signed yet, so Windows SmartScreen will
warn about an "unknown publisher." Click **More info -> Run anyway**. The full
source is right here in this repo if you want to read what went into the build.

Windows x64 only for now.

## 2. Point Crate at your music

Launch Crate, open **Preferences > Library**, and add your music folder. Let the
scan finish — your tracks appear in the table. BPM and key analysis starts
automatically in the background on first launch (it only analyzes tracks that
need it).

## 3. Build your galaxy

Open **Preferences > Crate** and click **Analyze library**. On the first run it
sets itself up and downloads the audio model (about a gigabyte, one time). The
map appears when analysis finishes; it refreshes itself when you return to Crate.

Rough timing: about 30-40 minutes per 1000 tracks on CPU, or minutes on a decent
NVIDIA GPU. Re-runs are incremental — only new tracks are analyzed.

The audio model is MuQ-MuLan by Tencent AI Lab (CC-BY-NC 4.0 — personal,
non-commercial use; downloaded from Hugging Face on first run, never bundled).

### Advanced/manual analysis

Power users can run `setup.ps1`, then `analyze.ps1 -Root "C:\path\to\music"`
from the installed `tools/analysis` folder. Linux and macOS users can try
`setup.sh` and run `analyze_all.py` from its environment; those paths are
best-effort for now.

## 4. Controllers

Plug in your controller and open **Preferences > Controllers**. Crate ships the
full Mixxx mapping library (300+ controllers).

- **Pioneer DDJ-FLX4 owners**: pick **"Pioneer DDJ-FLX4 (Crate)"** — the stock
  mapping plus map controls (browse-knob map navigation, load-from-map, view
  toggles) pre-bound.
- **Any other controller**: pick your stock mapping, then use **MIDI learn**
  (the learning wizard in the controller preferences) to bind the Crate map
  controls — they're listed under the **Crate** category (map/table knob focus,
  load cursor to deck, 3D, plexus, trail, layout, color, refresh).

The browse knob drives the library table normally; hit **KNOB:MAP** in the MAP
sidebar (or its MIDI binding) and the same knob walks the galaxy instead —
nearest-neighbor hops from track to track. LOAD loads whatever the map cursor
is on.

## 5. Getting help

Something broken or confusing? Open an issue on this repo (or tell whoever
pointed you here). This is an alpha — feedback goes straight into the next
round, and so do breaking changes.
