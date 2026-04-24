-- ─── events: one row per detected/ingested alarm ──────────────────
CREATE TABLE IF NOT EXISTS events (
    id           TEXT PRIMARY KEY,            -- uuid4 hex
    ts           INTEGER NOT NULL,            -- event time (unix ms)
    channel      TEXT NOT NULL,               -- cam0..camN
    source       TEXT NOT NULL,               -- 'motion' | 'webhook' | 'manual'
    snapshots    TEXT,                        -- JSON array of relative jpg paths
    clip_start   INTEGER,                     -- unix ms (event_ts - pre_event_sec)
    clip_end     INTEGER,                     -- unix ms (event_ts + post_event_sec)
    status       TEXT NOT NULL DEFAULT 'pending', -- pending | analyzed | failed
    description  TEXT,                        -- VLM-generated natural-language summary
    severity     TEXT,                        -- low | medium | high
    tags         TEXT,                        -- JSON array of strings
    raw_motion   REAL,                        -- foreground-area ratio at trigger
    extra        TEXT,                        -- JSON freeform for webhook payloads
    created_at   INTEGER NOT NULL,
    analyzed_at  INTEGER
);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);
CREATE INDEX IF NOT EXISTS idx_events_status ON events(status);
CREATE INDEX IF NOT EXISTS idx_events_channel_ts ON events(channel, ts);

-- ─── daily_summary: one row per calendar day ──────────────────────
CREATE TABLE IF NOT EXISTS daily_summary (
    date           TEXT PRIMARY KEY,          -- YYYY-MM-DD (local)
    total_events   INTEGER NOT NULL DEFAULT 0,
    high_count     INTEGER NOT NULL DEFAULT 0,
    medium_count   INTEGER NOT NULL DEFAULT 0,
    low_count      INTEGER NOT NULL DEFAULT 0,
    highlights     TEXT,                      -- JSON: [{time, channel, summary, severity}]
    channel_stats  TEXT,                      -- JSON: {cam0: 12, cam1: 3, ...}
    narrative      TEXT,                      -- LLM free-form daily report (markdown)
    generated_at   INTEGER NOT NULL,
    model          TEXT
);
