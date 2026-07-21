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

- Crate preferences seam (2026-07-19): CMakeLists.txt (+Crate-owned preferences page
  and test), src/preferences/dialog/dlgpreferences.cpp (registers the Crate page directly
  after Library, near the top of the preferences tree). All page implementation lives
  under src/crate/prefs; sampler numbering is skin-local under res/skins/Crate.

- Playlist-to-crate merge seam (2026-07-19, wave 7 slice C): CMakeLists.txt
  (+playlist migrator source/test), src/library/library.h and .cpp (`addFeature` gains a
  `showInSidebar` default; Setlog remains in the bind list but skips the sidebar;
  PlaylistFeature stays constructed and connected but is not registered; startup calls
  the one-shot public-API migrator after CrateFeature registration),
  src/mixxxmainwindow.cpp (removes the New Playlist signal wiring), and
  src/widget/wtrackmenu.cpp (centrally masks `Feature::Playlist` from all construction
  sites while leaving AutoDJ and Crate flags intact).
- CrateGalaxy seam: CMakeLists.txt (src/crate/*.cpp in source list),
  src/skin/legacy/legacyskinparser.h (+parseCrateGalaxy decl),
  src/skin/legacy/legacyskinparser.cpp (+include, +dispatch branch, +parseCrateGalaxy impl;
  2026-07-19: parseCrateGalaxy now passes m_pLibrary to WCrateGalaxy so the MAP-walk cursor
  can selection-sync the library table — the preferred FLX4 browse-knob load integration),
  2026-07-19: parseCrateGalaxy also reads optional GroundColor/InkColor/AccentDeckA/
  AccentDeckB children after scheme-variable substitution and passes that palette to
  WCrateGalaxy.
  src/skin/legacy/tooltips.cpp (+show_crate_galaxy tooltip)
- Crate intelligence seam: CMakeLists.txt (SonicVectors/scores sources and golden test)
- FX out-of-box default seam (2026-07-19): src/effects/effectsmanager.h (+decl
  loadCrateDefaultStandardEffects), src/effects/effectsmanager.cpp (+include
  effectpreset.h; readEffectsXml now seeds a default effect into all four standard units
  -- Echo, Reverb, Filter, Filter -- whenever NO effect is loaded in ANY standard unit slot. This
  covers both a virgin profile (empty preset list) AND a saved profile whose slots
  are all empty (FX never used). Any single loaded effect anywhere disables the seed,
  so a real setup is never clobbered; the fix self-heals on next launch. Routing
  (unit N -> deck N) is unchanged upstream behaviour from StandardEffectChain's ctor.
  Small, additive; low merge risk.
- Missing-control polling seam (2026-07-20): src/control/control.cpp remembers missing
  ConfigKeys already warned about, so controller polling logs each absent target once
  instead of once per poll.
- Track-menu warning seam (2026-07-20): src/widget/wtrackmenu.cpp creates the two Hotcue
  sorting actions with Metadata (the menu that consumes them), not ResetPlayed. Triage
  supports metadata without ResetPlayed and previously inserted two null actions at startup.
- Galaxy search seam (2026-07-20): src/crate/galaxy/wcrategalaxy.cpp defers table-model
  reset notifications until BaseSqlTableModel finishes replacing rows and resolves each
  result through getTrack(), because getTrackLocation() is empty for uncached rows.
- FX 4-deck routing fix (2026-07-20): CMakeLists.txt (+src/test/crate_fx_routing_test.cpp),
  src/engine/effects/engineeffect.h and .cpp, src/effects/effectslot.cpp. Bug: effects had
  no audible effect on decks 3/4 in 4-deck mode. EngineEffect's per-channel enable-state
  matrix (m_effectEnableStateForChannelMatrix) was only populated in the ctor and only
  ever flipped -- never inserted -- by SET_EFFECT_PARAMETERS. Decks 3/4 are registered as
  effect input channels only AFTER EffectsManager::setup() has built the standard units and
  seeded Echo/Reverb (the Crate skin raises [App],num_decks to 4 at skin-load, well after
  coreservices' addConfiguredDecks + setup()). A channel missing from the matrix is treated
  as Disabled by EngineEffect::process(), so the loaded effect silently ignored it, even
  though the group_[ChannelN]_enable CO existed and toggled and the CHAIN-level late-add
  path (EngineEffectChain::enableForInputChannel) worked. Fix: EngineEffect::initializeInputChannel
  now takes the effect's current enable state and, mirroring the ctor, inserts a missing
  input channel into the matrix (set to Enabling when the effect is on) -- same main-thread,
  pre-ENABLE-message ordering the EffectProcessor state allocation already relies on, and the
  channel is not routed to the effect until the later chain ENABLE message. EffectSlot::initializeInputChannel
  passes m_pControlEnabled->toBool() through; its own signature and EffectChain are unchanged.
  Small, additive, no behaviour change for decks present at load. This also fixes the latent
  upstream case of any input channel (e.g. skin-added samplers) registered after an effect is
  loaded and then routed to that effect's standard unit.
- identity rename pass: CMakeLists.txt, src/config.h.in, src/dialog/dlgabout.cpp,
  src/dialog/dlgaboutdlg.ui, src/mixxx.rc, src/util/cmdlineargs.cpp,
  src/util/versionstore.cpp
- Auto-analyze seam (2026-07-19, wave 7): src/library/library.h (+slotAutoAnalyzeLibrary
  decl, +maybeAutoAnalyzeLibrary + 3 bool one-shot state members), src/library/library.cpp
  (+include crate/autoanalysis.h; scanStarted/scanFinished lambdas track scan state;
  onSkinLoadFinished arms the one-shot; slotAutoAnalyzeLibrary enqueues the whole library
  via the existing analyzeTracks signal — AnalyzerBeats::shouldAnalyze skips analyzed
  tracks so this is idempotent), src/library/analysis/analysisfeature.cpp (worker count
  now crate::analyzerThreadCount: [Crate],analyzer_threads override, else
  idealThreadCount/2 floor 1 — was all cores). Logic lives in src/crate/autoanalysis.h.
- App-wide font seam (2026-07-19, wave 7): src/coreservices.cpp (+include QFont; after
  FontUtils::initializeFonts, QApplication::setFont("IBM Plex Mono") at the current
  default point size so dialogs/preferences stop rendering the OS default font; skin QSS
  still wins where it specifies fonts. Small, additive; low merge risk.)
- Crate splash/packaged logo: res/images/mixxx_logo.svg (flat #05060a field with
  monochrome CRATE wordmark; intentionally replaces upstream Mixxx branding)
- Skin identity de-grey (2026-07-19): res/images/library/*.svg recolored to Crate mono
  palette (gradient stops #ff6600/#de5800 -> #9aa3b2/#6b7280, #646464/#3c3c3c ->
  #565e6d/#2b3140, flat #979797/#b3b3b3 -> #7f8694, flat orange -> #b4d2ff) and
  cover_default.svg dimmed to ink (#2e3442/#4a5262). These are qrc-embedded — visual
  changes require a rebuild.
- Full identity pass (2026-07-19, "Crate mixed with rekordbox"): res/skins/Crate only,
  all disk-loaded (NO rebuild needed). (a) FONT: both font-family decls in style.qss now
  "IBM Plex Mono","Cascadia Mono","Consolas" (was Open Sans at l.935); IBM Plex Mono TTFs
  auto-load from res/fonts. (b) DE-DEERE: every url() in style.qss repointed from
  skin:/../Deere/ to skin-local copies (grep "Deere/" style.qss now empty); the 52
  Deere-referenced icons/images already existed as byte-identical Crate copies and were
  recolored in place. Recolor map: active-state orange #f60/#ff6600 -> cold-blue #b4d2ff
  (checkbox-checked, bpm-locked, preview-pause, mainmenu-check, checkmark, sort arrows,
  lib-clear-search focus, AutoDJ crossfader); neutral-fg greys #d2d2d2/#bfbfbf/#b3b3b3/etc
  -> ink #f4f7fb; off/disabled greys -> dim slate #5b626e; other-blues #4495f4/#00a7f8
  -> #b4d2ff. (c) KNOWN OFFENDERS: mixer GREEN knob rings (knob_bg_green*.svg,
  knob_small_green.svg) #5bd97a -> neutral translucent ink (green reserved for
  play/sync transport); FX mix-mode purple #c143ff (ic_fx_mixmode_d±w) -> cold-blue;
  library sort-indicator orange (image/style_sort_*.svg, rewritten clean minimal
  cold-blue). (d) QSS leaks: orange :focus/:hover/edit borders #FF6600 and rgba(255,102,0)
  and purple #711ada (repeating-sampler) -> cold-blue. (e) NEW files: 2 indeterminate
  checkmark svgs (were dangling refs, missing in both Crate and Deere). Deck blue/amber
  handle pairing (#b4d2ff / #ffb454) left intact per steer; amber #ffb454 + carmine
  #ff7878 + red kill/inversion left as reserved warm/destructive.

## Known issues
- Skin manifest <attribute> DEFAULTS not applied on a virgin profile (sections default
  visible until their [Skin] config keys exist; explicit config honored correctly).
  Affects first-run UX only; investigate in a later slice (LegacySkinParser attribute path).

## Repo-side (non-file) divergences

- 2026-07-18: GitHub Actions upstream workflows disabled via repo settings/API (no file edits);
  only `crate-windows.yml` runs. Actions minutes discipline: private repo, Windows runners.
