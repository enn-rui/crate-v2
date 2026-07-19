# Crate fork notes — the merge-pain ledger

Crate v2 is an identity-hard / engine-soft fork of Mixxx: our product, but git history
stays connected to `upstream` (mixxxdj/mixxx) and we cherry-pick engine/controller/bugfix
commits. To keep merges cheap, ALL new Crate code lives in:

- `src/crate/`           (intelligence, galaxy, data readers)
- `res/skins/Crate/`     (the ink-on-black skin)
- `src/test/crate_*`     (golden tests)
- `.github/workflows/crate-*.yml` (our CI; upstream workflows are disabled repo-side, files untouched)

Upstream files may be edited ONLY at registration seams, and every such edit MUST be
listed here. This list is the entire merge-conflict surface for future `upstream/main` pulls.

## Upstream files edited (keep current!)

- CrateGalaxy seam: CMakeLists.txt (src/crate/*.cpp in source list),
  src/skin/legacy/legacyskinparser.h (+parseCrateGalaxy decl),
  src/skin/legacy/legacyskinparser.cpp (+include, +dispatch branch, +parseCrateGalaxy impl),
  src/skin/legacy/tooltips.cpp (+show_crate_galaxy tooltip)
- Crate intelligence seam: CMakeLists.txt (SonicVectors/scores sources and golden test)
- identity rename pass: CMakeLists.txt, src/config.h.in, src/dialog/dlgabout.cpp,
  src/dialog/dlgaboutdlg.ui, src/mixxx.rc, src/util/cmdlineargs.cpp,
  src/util/versionstore.cpp
- Crate splash/packaged logo: res/images/mixxx_logo.svg (flat #05060a field with
  monochrome CRATE wordmark; intentionally replaces upstream Mixxx branding)
- Skin identity de-grey (2026-07-19): res/images/library/*.svg recolored to Crate mono
  palette (gradient stops #ff6600/#de5800 -> #9aa3b2/#6b7280, #646464/#3c3c3c ->
  #565e6d/#2b3140, flat #979797/#b3b3b3 -> #7f8694, flat orange -> #b4d2ff) and
  cover_default.svg dimmed to ink (#2e3442/#4a5262). These are qrc-embedded — visual
  changes require a rebuild.

## Known issues
- Skin manifest <attribute> DEFAULTS not applied on a virgin profile (sections default
  visible until their [Skin] config keys exist; explicit config honored correctly).
  Affects first-run UX only; investigate in a later slice (LegacySkinParser attribute path).

## Repo-side (non-file) divergences

- 2026-07-18: GitHub Actions upstream workflows disabled via repo settings/API (no file edits);
  only `crate-windows.yml` runs. Actions minutes discipline: private repo, Windows runners.
