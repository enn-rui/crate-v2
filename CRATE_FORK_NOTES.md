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

- identity rename pass: CMakeLists.txt, src/config.h.in, src/dialog/dlgabout.cpp,
  src/dialog/dlgaboutdlg.ui, src/mixxx.rc, src/util/cmdlineargs.cpp,
  src/util/versionstore.cpp
- Crate splash/packaged logo: res/images/mixxx_logo.svg (flat #05060a field with
  monochrome CRATE wordmark; intentionally replaces upstream Mixxx branding)

## Known issues
- Skin manifest <attribute> DEFAULTS not applied on a virgin profile (sections default
  visible until their [Skin] config keys exist; explicit config honored correctly).
  Affects first-run UX only; investigate in a later slice (LegacySkinParser attribute path).

## Repo-side (non-file) divergences

- 2026-07-18: GitHub Actions upstream workflows disabled via repo settings/API (no file edits);
  only `crate-windows.yml` runs. Actions minutes discipline: private repo, Windows runners.
