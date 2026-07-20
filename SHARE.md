# Crate — friend quickstart

Crate is a DJ app built on the Mixxx engine: a rekordbox-style layout, a sonic
galaxy map of your library (tracks that sound alike sit near each other), smart
crates, and full DJ controller support. This guide takes you from zero to mixing
off the map.

## 1. Install

Download the latest `.msi` from this repo's **Releases** page and run it.

Honest caveat: the installer is not code-signed yet, so Windows SmartScreen will
warn about an "unknown publisher." Click **More info -> Run anyway**. You have
access to this repo, so you can read every line that went into the build.

Windows x64 only for now.

## 2. Point Crate at your music

Launch Crate, open **Preferences > Library**, and add your music folder. Let the
scan finish — your tracks appear in the table. BPM and key analysis starts
automatically in the background on first launch (it only analyzes tracks that
need it).

## 3. Build your galaxy (one-time setup, then one command)

The MAP view needs audio analysis that runs outside the app. From the repo's
`tools/analysis/` folder, in PowerShell:

```
.\setup.ps1
```

One-time environment setup. Expect a large download (PyTorch, plus an audio
model of about a gigabyte on first analysis). If you have an NVIDIA GPU it will
be used automatically; otherwise everything works on CPU.

Then, whenever you add music:

```
.\analyze.ps1 -Root "C:\path\to\your\music"
```

Rough timing: about 30-40 minutes per 1000 tracks on CPU, minutes on a decent
GPU. Re-runs are incremental — only new tracks are analyzed.

The audio model is MuQ-MuLan by Tencent AI Lab (CC-BY-NC 4.0 — personal,
non-commercial use; downloaded from Hugging Face on first run, never bundled).

When it finishes, set **Preferences > Crate > sidecar folder** to the `.crate`
folder inside your music folder (first time only), then hit the **REFRESH** row
in the MAP sidebar (or restart). Your galaxy appears: every dot is a track,
neighborhoods are sounds.

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

Something broken or confusing? Tell the person who gave you access — this is a
small private build and feedback goes straight into the next round.
