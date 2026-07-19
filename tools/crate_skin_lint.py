#!/usr/bin/env python3
"""Crate v2 skin-lint (wave-5 S4).

A no-GUI, no-build sanity check for the two skin regressions that only a live
capture can normally catch:

  1. QComboBox flavors whose drop-down arrow overlaps the (IBM Plex Mono) text,
     i.e. a combo rule with no `padding-right`, or whose popup list is left
     native-white / stock-grey because `QComboBox QAbstractItemView` was never
     styled dark.
  2. Stock-Deere grey (the "#333 / #444 / #666" family and the warm-grey
     #201f1f / #404040 / #444342 / #565353 / #aaa / #c1cabe popup palette)
     lingering in the FX unit and mic/aux strips after the wave-4 identity pass.

This is deliberately a STATIC, selector-scoped check. It does NOT resolve the
full CSS cascade -- the authoritative visual gate is the S5 fresh-capture diff
after the app is relinked. It exists to stop obvious regressions from landing.

Exit 0 = clean, non-zero = one or more findings printed.

Run:  python tools/crate_skin_lint.py
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QSS = os.path.join(REPO_ROOT, "res", "skins", "Crate", "style.qss")

# Stock-Deere greys the Crate ink palette replaced. These must not appear as a
# color VALUE inside the in-scope FX / mic / combo-popup rule blocks below.
BANNED_GREYS = [
    "#333", "#444", "#666",
    "#201f1f", "#404040", "#444342", "#565353",
    "#aaa", "#c1cabe", "#1f1e1e",
]

# In-scope selector blocks (FX unit rows + mic/aux strips + FX combo popups)
# whose declaration bodies must be grey-free. Each entry is the exact selector
# header text that opens the rule in style.qss.
SCOPED_SELECTOR_HEADERS = [
    "#EffectUnitControls {",
    "#MicAuxRack {",
    "#MicAuxContainer, #MicDuckingContainer {",
]


def read_qss():
    with open(QSS, "r", encoding="utf-8") as handle:
        text = handle.read()
    # Strip /* ... */ comments so a hex named in a comment (e.g. "was #333")
    # is never mistaken for a live color value.
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)


def rule_body(text, header):
    """Return the declaration body between `header` and its closing brace."""
    idx = text.find(header)
    if idx < 0:
        return None
    start = idx + len(header)
    end = text.find("}", start)
    if end < 0:
        return None
    return text[start:end]


def check_scoped_greys(text, findings):
    for header in SCOPED_SELECTOR_HEADERS:
        body = rule_body(text, header)
        if body is None:
            findings.append(
                "MISSING RULE: expected selector `%s` not found in style.qss" % header)
            continue
        # Match a hex only when it is a full token (word-boundaryish): a '#'
        # followed by hex digits and not immediately followed by another hex
        # digit (so "#333" does not spuriously match inside "#333b4a").
        for grey in BANNED_GREYS:
            pattern = re.escape(grey) + r"(?![0-9a-fA-F])"
            if re.search(pattern, body):
                findings.append(
                    "GREY IN FX/MIC: banned %s inside `%s` -> use Crate ink "
                    "(#0d1017 / #05060a)." % (grey, header.rstrip(" {")))


def check_combobox_structural(text, findings):
    # Generic combo must trim the arrow and style its popup dark.
    if not re.search(r"(?m)^QComboBox\s*\{[^}]*padding-right\s*:", text):
        findings.append(
            "COMBO ARROW: generic `QComboBox { ... }` has no padding-right -> "
            "the drop-down arrow can overlap Plex Mono text.")
    if "QComboBox::drop-down" not in text:
        findings.append(
            "COMBO ARROW: no `QComboBox::drop-down` rule to size/place the "
            "drop-down button.")
    popup = rule_body(text, "QComboBox QAbstractItemView {")
    if popup is None:
        findings.append(
            "WHITE POPUP: no `QComboBox QAbstractItemView { ... }` rule -> the "
            "generic combo popup renders native-white/unreadable.")
    elif "background-color" not in popup:
        findings.append(
            "WHITE POPUP: `QComboBox QAbstractItemView` sets no background-color.")


def check_fx_popup_ink(text, findings):
    # Every FX/search/AutoDJ combo flavor's popup view must be styled dark ink,
    # because on Windows the popup does not inherit the combo's own colors.
    flavors = [
        "WEffectSelector QAbstractItemView",
        "WEffectChainPresetSelector QAbstractItemView",
        "WSearchLineEdit QAbstractItemView",
        "#fadeModeCombobox QAbstractItemView",
    ]
    for flavor in flavors:
        # Accept the flavor appearing in any grouped selector list that carries
        # a dark background-color declaration.
        found_dark = False
        for m in re.finditer(re.escape(flavor), text):
            brace = text.find("{", m.end())
            close = text.find("}", brace) if brace >= 0 else -1
            if brace < 0 or close < 0:
                continue
            body = text[brace + 1:close]
            if "background-color" in body and ("#05060a" in body or "#0d1017" in body):
                found_dark = True
                break
        if not found_dark:
            findings.append(
                "WHITE POPUP: `%s` popup is not styled dark ink -> may render "
                "grey/white." % flavor)


def main():
    if not os.path.isfile(QSS):
        print("crate_skin_lint: cannot find %s" % QSS, file=sys.stderr)
        return 2
    text = read_qss()
    findings = []
    check_scoped_greys(text, findings)
    check_combobox_structural(text, findings)
    check_fx_popup_ink(text, findings)

    if findings:
        print("crate_skin_lint: %d finding(s):" % len(findings))
        for item in findings:
            print("  - " + item)
        return 1
    print("crate_skin_lint: OK (FX/mic strips grey-free; combo padding + dark "
          "popups present for every flavor).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
