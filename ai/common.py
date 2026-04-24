"""Shared helpers: config loading, sqlite connections, logging, paths.

Kept dependency-free except for PyYAML so it can be imported safely
by event_engine, vlm_worker, summary_job and api_server.
"""

from __future__ import annotations

import logging
import os
import sqlite3
import sys
import time
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Any

import yaml

CONFIG_PATH = Path(__file__).parent / "config.yaml"
SCHEMA_PATH = Path(__file__).parent / "schema.sql"


def load_config(path: Path | str = CONFIG_PATH) -> dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def setup_logging(name: str, cfg: dict[str, Any]) -> logging.Logger:
    """Console + rotating file handler, writes under <log_dir>/<name>.log."""
    log_cfg = cfg.get("logging", {})
    level = getattr(logging, log_cfg.get("level", "INFO").upper(), logging.INFO)
    log_dir = Path(log_cfg.get("log_dir", "/tmp/ai_logs"))
    log_dir.mkdir(parents=True, exist_ok=True)

    logger = logging.getLogger(name)
    logger.setLevel(level)
    if logger.handlers:
        return logger

    fmt = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    sh = logging.StreamHandler(sys.stdout)
    sh.setFormatter(fmt)
    logger.addHandler(sh)

    fh = RotatingFileHandler(
        log_dir / f"{name}.log",
        maxBytes=20 * 1024 * 1024,
        backupCount=5,
        encoding="utf-8",
    )
    fh.setFormatter(fmt)
    logger.addHandler(fh)

    return logger


def ensure_db(db_path: str | Path) -> None:
    """Create db file + apply schema if not present."""
    db_path = Path(db_path)
    db_path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(str(db_path)) as conn:
        conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
        conn.commit()


def open_db(db_path: str | Path) -> sqlite3.Connection:
    """Open a connection with WAL + row factory for dict-like access.

    WAL improves concurrent reader/writer behaviour, important because
    event_engine, vlm_worker, summary_job and api_server all touch the
    same db simultaneously.
    """
    conn = sqlite3.connect(str(db_path), timeout=10.0, isolation_level=None)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


def now_ms() -> int:
    return int(time.time() * 1000)


def ensure_dir(path: str | Path) -> Path:
    p = Path(path)
    p.mkdir(parents=True, exist_ok=True)
    return p


def snapshot_dir_for(snapshot_root: str | Path, event_id: str) -> Path:
    """Per-event snapshot directory: <root>/<yyyymmdd>/<event_id>/."""
    day = time.strftime("%Y%m%d", time.localtime())
    return ensure_dir(Path(snapshot_root) / day / event_id)


def relative_snapshot_path(snapshot_root: str | Path, abs_path: str | Path) -> str:
    """Return the path relative to the snapshot root, used for storing in DB
    and serving via the API (so the deployment IP is not baked into the row).
    """
    return str(Path(abs_path).relative_to(Path(snapshot_root)))


def graceful_shutdown_signals(handler):
    """Wire SIGINT/SIGTERM to the given handler. Safe in main thread only."""
    import signal
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(sig, handler)
        except (ValueError, OSError):
            pass


def env_or(key: str, default: str) -> str:
    return os.environ.get(key, default)
