#pragma once

namespace crate {

// Bar-phase math for 4/4 bar markers.
//
// Mixxx beatgrids carry no downbeat/bar information, so the Crate fork derives
// bars the way rekordbox does: a bar starts every 4th beat, counted from the
// grid anchor (Beats::cfirstmarker(), i.e. beat index 0), plus an optional
// per-track downbeat offset in [0,3] because the anchor is not guaranteed to be
// beat one.
//
// `beatIndex` is the beat's index relative to the grid anchor. It may be
// negative for beats before the anchor (preroll). It is obtained by iterator
// subtraction against Beats::cfirstmarker(), which counts through variable
// tempo sections without drift (BeatMarker::beatsTillNextMarker() is always a
// positive integer, so counting is always exact).

// Normalize a raw offset into the canonical [0,3] range.
constexpr int normalizeDownbeatOffset(int offset) {
    return ((offset % 4) + 4) % 4;
}

// rekordbox "Battito": the beat's position inside its bar, 1..4, where 1 is the
// downbeat (bar line).
constexpr int barBattito(int beatIndex, int downbeatOffset) {
    return (((beatIndex - normalizeDownbeatOffset(downbeatOffset)) % 4) + 4) % 4 + 1;
}

// True when the beat at `beatIndex` starts a bar (is a downbeat).
constexpr bool isDownbeat(int beatIndex, int downbeatOffset) {
    return barBattito(beatIndex, downbeatOffset) == 1;
}

} // namespace crate
