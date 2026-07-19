"""Golden-fixture generator for the Crate v2 intelligence port.

Runs Crate v1's library.py (the reference spec) over a small deterministic fixture
subset of the real library and emits golden.json — the acceptance contract every C++
intelligence slice must match (bit-exact centered vectors; 1e-6 scalars; exact ranked
order). Re-runnable; fixtures + JSON are committed into the fork under golden/.

Usage: python gen_fixtures.py  (from anywhere; paths are absolute below)
"""
import json
import shutil
import sqlite3
import struct
import sys
from pathlib import Path

DEV_SIDECARS = Path(r"C:\Users\ryanl\Desktop\AGENTIC\crate-v2\devdata\.crate")
OUT = Path(__file__).resolve().parent / "out"
FIX_ROOT = OUT / "fixture_lib"          # acts as lib_root; sidecars in .crate/
FIX_SIDE = FIX_ROOT / ".crate"
FIX_DB = OUT / "crate.db"
GOLDEN = OUT / "golden.json"
N_TRACKS = 30

sys.path.insert(0, r"C:\Users\ryanl\Desktop\AGENTIC\apps\crate")


def f64hex(x: float) -> str:
    return struct.pack("<d", float(x)).hex()


def pick_fixture_relpaths() -> list[str]:
    vec = sqlite3.connect(DEV_SIDECARS / "music_vectors.sqlite")
    have_vec = {r[0] for r in vec.execute(
        "SELECT relpath FROM vectors WHERE vec IS NOT NULL AND vec_intro IS NOT NULL "
        "AND vec_outro IS NOT NULL")}
    vec.close()
    feat = sqlite3.connect(DEV_SIDECARS / "features.sqlite")
    have_feat = {r[0] for r in feat.execute(
        "SELECT relpath FROM features WHERE bpm IS NOT NULL AND key_camelot IS NOT NULL")}
    feat.close()
    um = sqlite3.connect(DEV_SIDECARS / "umap.sqlite")
    have_coord = {r[0] for r in um.execute("SELECT relpath FROM coords")}
    um.close()
    eligible = sorted(have_vec & have_feat & have_coord)
    if len(eligible) < N_TRACKS:
        raise SystemExit(f"only {len(eligible)} eligible tracks")
    step = len(eligible) // N_TRACKS
    return [eligible[i * step] for i in range(N_TRACKS)]


def build_fixture_sidecars(relpaths: list[str]) -> None:
    if OUT.exists():
        shutil.rmtree(OUT)
    FIX_SIDE.mkdir(parents=True)
    marks = ",".join("?" * len(relpaths))
    # music_vectors: chosen rows + the REAL persisted dataset mean (production behavior).
    src = sqlite3.connect(DEV_SIDECARS / "music_vectors.sqlite")
    dst = sqlite3.connect(FIX_SIDE / "music_vectors.sqlite")
    dst.execute("CREATE TABLE vectors (relpath TEXT PRIMARY KEY, dim INTEGER, vec BLOB, "
                "vec_intro BLOB, vec_outro BLOB)")
    dst.execute("CREATE TABLE vector_stats (k TEXT PRIMARY KEY, v BLOB)")
    cols = [r[1] for r in src.execute("PRAGMA table_info(vectors)")]
    sel = "relpath, dim, vec, vec_intro, vec_outro" if "dim" in cols else \
          "relpath, NULL, vec, vec_intro, vec_outro"
    for row in src.execute(f"SELECT {sel} FROM vectors WHERE relpath IN ({marks})", relpaths):
        dst.execute("INSERT INTO vectors VALUES (?,?,?,?,?)", row)
    for row in src.execute("SELECT k, v FROM vector_stats"):
        dst.execute("INSERT INTO vector_stats VALUES (?,?)", row)
    dst.commit(); dst.close(); src.close()
    # features
    src = sqlite3.connect(DEV_SIDECARS / "features.sqlite")
    dst = sqlite3.connect(FIX_SIDE / "features.sqlite")
    dst.execute("CREATE TABLE features (relpath TEXT PRIMARY KEY, mtime REAL, bpm REAL, "
                "key_camelot TEXT, key_name TEXT, energy REAL, centroid REAL, mfcc BLOB, "
                "analyzed_at TEXT, lufs REAL, danceability REAL)")
    for row in src.execute(
            "SELECT relpath, mtime, bpm, key_camelot, key_name, energy, centroid, mfcc, "
            f"analyzed_at, lufs, danceability FROM features WHERE relpath IN ({marks})",
            relpaths):
        dst.execute("INSERT INTO features VALUES (?,?,?,?,?,?,?,?,?,?,?)", row)
    dst.commit(); dst.close(); src.close()
    # clusters + umap (map fidelity extras, small)
    for name, table, cols_sql, sel_sql in (
            ("clusters.sqlite", "clusters", "relpath TEXT PRIMARY KEY, cluster_id INTEGER",
             "SELECT relpath, cluster_id FROM clusters"),
            ("umap.sqlite", "coords", "relpath TEXT PRIMARY KEY, x REAL, y REAL",
             "SELECT relpath, x, y FROM coords")):
        src = sqlite3.connect(DEV_SIDECARS / name)
        dst = sqlite3.connect(FIX_SIDE / name)
        dst.execute(f"CREATE TABLE {table} ({cols_sql})")
        q = "?" * sel_sql.count(",")
        for row in src.execute(f"{sel_sql} WHERE relpath IN ({marks})", relpaths):
            dst.execute(f"INSERT INTO {table} VALUES ({','.join('?' * len(row))})", row)
        dst.commit(); dst.close(); src.close()


def main() -> None:
    relpaths = pick_fixture_relpaths()
    build_fixture_sidecars(relpaths)

    import library  # Crate v1 — the reference spec
    # Point every module-level path at the fixtures (config-independent).
    library.LIB_ROOT = FIX_ROOT
    library.VECTORS_PATH = FIX_SIDE / "music_vectors.sqlite"
    library.FEATURES_PATH = FIX_SIDE / "features.sqlite"
    library.CLUSTERS_PATH = FIX_SIDE / "clusters.sqlite"
    library.UMAP_PATH = FIX_SIDE / "umap.sqlite"
    library.DB_PATH = FIX_DB

    # Fixture crate.db with the v1 schema, rows from features.
    con = library.connect(FIX_DB)
    feat = sqlite3.connect(FIX_SIDE / "features.sqlite")
    for rel in relpaths:
        bpm, key, energy = feat.execute(
            "SELECT bpm, key_camelot, energy FROM features WHERE relpath=?", (rel,)).fetchone()
        p = str(FIX_ROOT / rel)
        con.execute(
            "INSERT OR REPLACE INTO tracks (path, bucket, artist, title, album, ext, size, "
            "mtime, duration, bpm, key, energy) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            (p, "dj", "", Path(rel).stem, "", Path(rel).suffix, 0, 0, 0.0, bpm, key, energy))
    con.commit()
    feat.close()

    vecs = library.load_vectors(library.VECTORS_PATH, library.LIB_ROOT, force=True)
    secs = library.load_section_vectors(library.VECTORS_PATH, library.LIB_ROOT, force=True)
    tracks = {}
    for r in con.execute("SELECT * FROM tracks"):
        t = library._row_to_track(r)
        tracks[t.path] = t
    con.close()
    path_of = {rel: str(FIX_ROOT / rel) for rel in relpaths}

    golden = {
        "meta": {"n": len(relpaths), "relpaths": relpaths,
                 "note": "keys are relpaths; C++ joins via its own lib_root"},
        "center": {}, "sonic": {}, "key_score": {}, "bpm_score": {},
        "transition": {}, "mixability": {}, "similar": {}, "compatible_next": {},
        "build_path": {}, "camelot": {},
    }
    for rel in relpaths:
        v = vecs.get(path_of[rel])
        golden["center"][rel] = v.tobytes().hex() if v is not None else None
    for ra in relpaths:
        for rb in relpaths:
            if ra == rb:
                continue
            k = f"{ra}|{rb}"
            ta, tb = tracks[path_of[ra]], tracks[path_of[rb]]
            s = library.sonic_similarity(path_of[ra], path_of[rb], vecs)
            golden["sonic"][k] = f64hex(s) if s is not None else None
            golden["key_score"][k] = f64hex(library._key_score(ta.key, tb.key))
            golden["bpm_score"][k] = f64hex(library._bpm_score(ta.bpm, tb.bpm))
            tr = library.transition_score(path_of[ra], path_of[rb], secs)
            golden["transition"][k] = f64hex(tr) if tr is not None else None
            golden["mixability"][k] = f64hex(
                library.mixability(ta, tb, vectors=vecs, sections=secs))
    seeds = relpaths[:5]
    for rel in seeds:
        out = library.similar_tracks(tracks[path_of[rel]], n=10, db_path=FIX_DB,
                                     vectors_path=library.VECTORS_PATH,
                                     lib_root=library.LIB_ROOT)
        golden["similar"][rel] = [
            [str(Path(t.path).relative_to(FIX_ROOT)).replace("\\", "/"), f64hex(s)]
            for t, s in out]
    for rel in seeds[:3]:
        out = library.compatible_next(tracks[path_of[rel]], db_path=FIX_DB, limit=10)
        golden["compatible_next"][rel] = [
            [str(Path(t.path).relative_to(FIX_ROOT)).replace("\\", "/"), f64hex(s)]
            for t, s in out]
    for rel, energy in ((seeds[0], "flat"), (seeds[1], "up")):
        out = library.build_path(tracks[path_of[rel]], length=6, db_path=FIX_DB, energy=energy)
        golden["build_path"][f"{rel}|{energy}"] = [
            str(Path(t.path).relative_to(FIX_ROOT)).replace("\\", "/") for t in out]
    for num in range(1, 13):
        for let in "AB":
            code = f"{num}{let}"
            golden["camelot"][code] = library.camelot_neighbors(code)
    for bad in ("", "13A", "0B", "Am", "F#m", "99"):
        golden["camelot"][bad or "<empty>"] = library.camelot_neighbors(bad) if bad else {}

    GOLDEN.write_text(json.dumps(golden, indent=1, sort_keys=True), encoding="utf-8")
    print(f"golden.json written: {GOLDEN.stat().st_size} bytes, "
          f"{len(relpaths)} tracks, {len(golden['mixability'])} pair scores")


if __name__ == "__main__":
    main()
