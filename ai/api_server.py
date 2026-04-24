"""FastAPI server exposing events / summary / snapshot / clip endpoints
to the VMS web frontend.

URL conventions
---------------
GET  /health                              liveness probe
GET  /events                              list, filterable by date/channel/severity
GET  /events/{id}                         single event detail (incl. playback url)
GET  /events/{id}/snapshot/{idx}          raw jpg
GET  /events/{id}/clip                    302 → MediaMTX playback url
POST /events/{id}/reanalyze               flip status back to 'pending'
POST /events/external                     ingest event from external NVR/webhook
GET  /summary                             daily summary by date
GET  /summary/dates                       list of dates that have summaries

CORS is fully open since this runs in a LAN context behind the same router
as the browser.
"""

from __future__ import annotations

import json
import sqlite3
import time
import uuid
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional
from urllib.parse import urlencode

import yaml
from fastapi import Depends, FastAPI, HTTPException, Query, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, RedirectResponse
from pydantic import BaseModel, Field

from common import (
    ensure_db,
    load_config,
    now_ms,
    open_db,
    setup_logging,
)


# ─── Lifespan ──────────────────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    cfg = load_config()
    log = setup_logging("api_server", cfg)
    db_path = cfg["storage"]["db_path"]
    ensure_db(db_path)
    app.state.cfg = cfg
    app.state.log = log
    app.state.db_path = db_path
    app.state.snapshot_root = Path(cfg["storage"]["snapshot_dir"])
    app.state.playback_base = cfg["mediamtx"]["playback_base"].rstrip("/")
    log.info("api_server starting on %s:%s",
             cfg["api"]["host"], cfg["api"]["port"])
    yield
    log.info("api_server stopped")


app = FastAPI(title="video-analyzer AI API", version="1.0.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
    allow_credentials=False,
)


def get_db(request: Request) -> sqlite3.Connection:
    conn = open_db(request.app.state.db_path)
    try:
        yield conn
    finally:
        conn.close()


# ─── Models ────────────────────────────────────────────────────────────
class EventOut(BaseModel):
    id: str
    ts: int
    time: str
    channel: str
    source: str
    status: str
    severity: Optional[str] = None
    description: Optional[str] = None
    tags: list[str] = []
    snapshots: list[str] = []
    snapshot_urls: list[str] = []
    clip_start: Optional[int] = None
    clip_end: Optional[int] = None
    clip_url: Optional[str] = None
    raw_motion: Optional[float] = None
    created_at: int
    analyzed_at: Optional[int] = None


class ExternalEventIn(BaseModel):
    channel: str = Field(..., description="cam0..camN")
    ts: Optional[int] = Field(None, description="event time (unix ms); now if missing")
    description: Optional[str] = None
    severity: Optional[str] = None
    tags: list[str] = []
    extra: dict = {}


class SummaryOut(BaseModel):
    date: str
    total_events: int
    high_count: int
    medium_count: int
    low_count: int
    highlights: list[dict]
    channel_stats: dict
    narrative: Optional[str] = None
    generated_at: Optional[int] = None
    model: Optional[str] = None


# ─── Helpers ───────────────────────────────────────────────────────────
def _row_to_event(row: sqlite3.Row, request: Request) -> EventOut:
    snapshots = json.loads(row["snapshots"] or "[]")
    tags = json.loads(row["tags"] or "[]") if row["tags"] else []
    base = str(request.base_url).rstrip("/")
    snap_urls = [f"{base}/events/{row['id']}/snapshot/{i}"
                 for i in range(len(snapshots))]

    clip_url = None
    if row["clip_start"] and row["clip_end"]:
        clip_url = f"{base}/events/{row['id']}/clip"

    return EventOut(
        id=row["id"],
        ts=row["ts"],
        time=time.strftime("%Y-%m-%d %H:%M:%S",
                           time.localtime(row["ts"] / 1000)),
        channel=row["channel"],
        source=row["source"],
        status=row["status"],
        severity=row["severity"],
        description=row["description"],
        tags=tags,
        snapshots=snapshots,
        snapshot_urls=snap_urls,
        clip_start=row["clip_start"],
        clip_end=row["clip_end"],
        clip_url=clip_url,
        raw_motion=row["raw_motion"],
        created_at=row["created_at"],
        analyzed_at=row["analyzed_at"],
    )


def _date_range_ms(date_str: str) -> tuple[int, int]:
    """Convert YYYY-MM-DD (local time) to [start_ms, end_ms)."""
    t = time.strptime(date_str, "%Y-%m-%d")
    start = int(time.mktime(t) * 1000)
    end = start + 24 * 3600 * 1000
    return start, end


# ─── Endpoints ─────────────────────────────────────────────────────────
@app.get("/health")
def health():
    return {"status": "ok", "ts": now_ms()}


@app.get("/events", response_model=list[EventOut])
def list_events(
    request: Request,
    date: Optional[str] = Query(None, description="YYYY-MM-DD (local). Default = today"),
    channel: Optional[str] = None,
    severity: Optional[str] = Query(None, regex="^(low|medium|high)$"),
    status: Optional[str] = Query(None, regex="^(pending|analyzed|failed)$"),
    since_ts: Optional[int] = Query(None, description="filter ts > since_ts (ms)"),
    limit: int = Query(100, ge=1, le=500),
    db: sqlite3.Connection = Depends(get_db),
):
    where = []
    params: list = []

    if since_ts is not None:
        where.append("ts > ?")
        params.append(since_ts)
    else:
        d = date or time.strftime("%Y-%m-%d", time.localtime())
        try:
            start, end = _date_range_ms(d)
        except ValueError:
            raise HTTPException(400, f"invalid date: {d}")
        where.append("ts >= ? AND ts < ?")
        params.extend([start, end])

    if channel:
        where.append("channel = ?")
        params.append(channel)
    if severity:
        where.append("severity = ?")
        params.append(severity)
    if status:
        where.append("status = ?")
        params.append(status)

    sql = (
        "SELECT * FROM events WHERE " + " AND ".join(where)
        + " ORDER BY ts DESC LIMIT ?"
    )
    params.append(limit)
    rows = db.execute(sql, params).fetchall()
    return [_row_to_event(r, request) for r in rows]


@app.get("/events/{event_id}", response_model=EventOut)
def get_event(event_id: str, request: Request,
              db: sqlite3.Connection = Depends(get_db)):
    row = db.execute("SELECT * FROM events WHERE id=?", (event_id,)).fetchone()
    if not row:
        raise HTTPException(404, "event not found")
    return _row_to_event(row, request)


@app.get("/events/{event_id}/snapshot/{idx}")
def get_snapshot(event_id: str, idx: int, request: Request,
                 db: sqlite3.Connection = Depends(get_db)):
    row = db.execute(
        "SELECT snapshots FROM events WHERE id=?", (event_id,)
    ).fetchone()
    if not row:
        raise HTTPException(404, "event not found")
    snapshots = json.loads(row["snapshots"] or "[]")
    if idx < 0 or idx >= len(snapshots):
        raise HTTPException(404, "snapshot index out of range")
    snap_root: Path = request.app.state.snapshot_root
    full = (snap_root / snapshots[idx]).resolve()
    # security: ensure inside snapshot_root
    try:
        full.relative_to(snap_root.resolve())
    except ValueError:
        raise HTTPException(400, "invalid snapshot path")
    if not full.exists():
        raise HTTPException(404, "snapshot file missing")
    return FileResponse(str(full), media_type="image/jpeg")


@app.get("/events/{event_id}/clip")
def get_clip(event_id: str, request: Request,
             db: sqlite3.Connection = Depends(get_db)):
    """302 to MediaMTX playback URL covering the clip window."""
    row = db.execute(
        "SELECT channel, clip_start, clip_end FROM events WHERE id=?",
        (event_id,),
    ).fetchone()
    if not row:
        raise HTTPException(404, "event not found")
    if not row["clip_start"] or not row["clip_end"]:
        raise HTTPException(404, "no clip window stored")

    duration = max(1, (row["clip_end"] - row["clip_start"]) // 1000)
    # MediaMTX playback expects RFC3339 timestamps.
    start_iso = time.strftime(
        "%Y-%m-%dT%H:%M:%S",
        time.localtime(row["clip_start"] / 1000),
    )
    base = request.app.state.playback_base
    qs = urlencode({
        "path": row["channel"],
        "start": start_iso,
        "duration": f"{duration}s",
        "format": "mp4",
    })
    return RedirectResponse(url=f"{base}/get?{qs}", status_code=302)


@app.post("/events/{event_id}/reanalyze")
def reanalyze(event_id: str, db: sqlite3.Connection = Depends(get_db)):
    row = db.execute("SELECT id FROM events WHERE id=?", (event_id,)).fetchone()
    if not row:
        raise HTTPException(404, "event not found")
    db.execute(
        "UPDATE events SET status='pending', description=NULL, severity=NULL, "
        "tags=NULL, analyzed_at=NULL WHERE id=?",
        (event_id,),
    )
    return {"id": event_id, "status": "pending"}


@app.post("/events/external", response_model=EventOut)
def ingest_external(payload: ExternalEventIn, request: Request,
                    db: sqlite3.Connection = Depends(get_db)):
    """Webhook endpoint: external NVR / IPC pushes an alarm here.

    If description is provided, the event is stored as already 'analyzed'
    (no VLM call). Otherwise it goes into the pending queue (but with no
    snapshots, so the VLM will fail unless caller also POSTs images later).
    """
    eid = uuid.uuid4().hex
    ts = payload.ts or now_ms()
    severity = (payload.severity or "medium").lower()
    if severity not in ("low", "medium", "high"):
        severity = "medium"
    status = "analyzed" if payload.description else "pending"
    db.execute(
        """INSERT INTO events
            (id, ts, channel, source, snapshots, clip_start, clip_end,
             status, description, severity, tags, extra,
             created_at, analyzed_at)
           VALUES (?, ?, ?, 'webhook', '[]', ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (eid, ts, payload.channel,
         ts - 1000, ts + 4000,
         status, payload.description, severity,
         json.dumps(payload.tags, ensure_ascii=False),
         json.dumps(payload.extra, ensure_ascii=False),
         now_ms(), now_ms() if status == "analyzed" else None),
    )
    row = db.execute("SELECT * FROM events WHERE id=?", (eid,)).fetchone()
    return _row_to_event(row, request)


@app.get("/summary", response_model=SummaryOut)
def get_summary(
    date: Optional[str] = Query(None, description="YYYY-MM-DD (local). Default = today"),
    db: sqlite3.Connection = Depends(get_db),
):
    d = date or time.strftime("%Y-%m-%d", time.localtime())
    row = db.execute("SELECT * FROM daily_summary WHERE date=?", (d,)).fetchone()
    if row:
        return SummaryOut(
            date=row["date"],
            total_events=row["total_events"],
            high_count=row["high_count"],
            medium_count=row["medium_count"],
            low_count=row["low_count"],
            highlights=json.loads(row["highlights"] or "[]"),
            channel_stats=json.loads(row["channel_stats"] or "{}"),
            narrative=row["narrative"],
            generated_at=row["generated_at"],
            model=row["model"],
        )

    # Live aggregate when no precomputed summary exists yet (e.g. today).
    try:
        start, end = _date_range_ms(d)
    except ValueError:
        raise HTTPException(400, f"invalid date: {d}")
    rows = db.execute(
        "SELECT severity, channel FROM events WHERE ts >= ? AND ts < ?",
        (start, end),
    ).fetchall()
    high = sum(1 for r in rows if r["severity"] == "high")
    medium = sum(1 for r in rows if r["severity"] == "medium")
    low = sum(1 for r in rows if r["severity"] == "low")
    chan_stats: dict[str, int] = {}
    for r in rows:
        chan_stats[r["channel"]] = chan_stats.get(r["channel"], 0) + 1
    return SummaryOut(
        date=d,
        total_events=len(rows),
        high_count=high,
        medium_count=medium,
        low_count=low,
        highlights=[],
        channel_stats=chan_stats,
        narrative=None,
        generated_at=None,
        model=None,
    )


@app.get("/summary/dates")
def list_summary_dates(db: sqlite3.Connection = Depends(get_db)):
    rows = db.execute(
        "SELECT date FROM daily_summary ORDER BY date DESC LIMIT 90"
    ).fetchall()
    return {"dates": [r["date"] for r in rows]}


# ─── Entrypoint ───────────────────────────────────────────────────────
def main() -> int:
    import uvicorn
    cfg = load_config()
    uvicorn.run(
        "api_server:app",
        host=cfg["api"]["host"],
        port=int(cfg["api"]["port"]),
        log_level="info",
        reload=False,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
