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

## Repo-side (non-file) divergences

- 2026-07-18: GitHub Actions upstream workflows disabled via repo settings/API (no file edits);
  only `crate-windows.yml` runs. Actions minutes discipline: private repo, Windows runners.
