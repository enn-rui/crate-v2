"""Cross-process embed mutex — one embedding model in RAM at a time.

Two multi-GB embedding processes sharing one host (this pipeline plus any other
model-holding indexer) can shove the machine deep into swap if both load their
models at once — hours of iowait and zero progress. This is an EXCLUSIVE
advisory lock on a shared lockfile so only one process can hold a model at a
time. Deployments that need it in more than one codebase carry an identical
copy of this file rather than cross-importing.

Lock path: env EMBED_LOCK_PATH, default ~/.embed.lock (expanded at runtime).
Linux uses fcntl.flock; Windows (where tests run) uses msvcrt; if neither is
available it degrades to a no-op with a warning (production is Linux-only).
"""
from __future__ import annotations

import contextlib
import os
import time

DEFAULT_TIMEOUT = 1800.0  # 30 min — generous; embed runs are minutes, not hours

try:
    import fcntl  # POSIX (the box)
    _BACKEND = "fcntl"
except ImportError:  # Windows
    try:
        import msvcrt
        _BACKEND = "msvcrt"
    except ImportError:
        _BACKEND = "none"


def _lock_path(path: str | None) -> str:
    return os.path.expanduser(
        path or os.environ.get("EMBED_LOCK_PATH") or "~/.embed.lock")


def _try_acquire(fh) -> bool:
    """Non-blocking attempt; True if the exclusive lock was taken."""
    if _BACKEND == "fcntl":
        try:
            fcntl.flock(fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False
    if _BACKEND == "msvcrt":
        try:
            fh.seek(0)
            msvcrt.locking(fh.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False
    return True  # no-op backend: always "acquired"


def _release(fh) -> None:
    if _BACKEND == "fcntl":
        fcntl.flock(fh.fileno(), fcntl.LOCK_UN)
    elif _BACKEND == "msvcrt":
        try:
            fh.seek(0)
            msvcrt.locking(fh.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass


@contextlib.contextmanager
def embed_lock(timeout: float = DEFAULT_TIMEOUT, path: str | None = None,
               poll: float = 1.0, log=print):
    """Block until the shared embed lock is held, then run the body holding it.

    Raises TimeoutError if it can't be acquired within `timeout` seconds.
    On the no-op backend (no fcntl/msvcrt) it warns once and proceeds unlocked.
    """
    p = _lock_path(path)
    d = os.path.dirname(p)
    if d:
        os.makedirs(d, exist_ok=True)

    if _BACKEND == "none":
        log(f"embed lock: no file-locking backend on this platform, running "
            f"WITHOUT a cross-process lock ({p})")
        yield p
        return

    fh = open(p, "a+")
    try:
        deadline = time.monotonic() + timeout
        warned = False
        while not _try_acquire(fh):
            if not warned:
                log(f"waiting for embed lock ({p}) — another embed run holds it")
                warned = True
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"could not acquire embed lock {p} within {timeout:.0f}s "
                    f"(another embedding run is still holding it)")
            time.sleep(poll)
        yield p
    finally:
        with contextlib.suppress(Exception):
            _release(fh)
        fh.close()
