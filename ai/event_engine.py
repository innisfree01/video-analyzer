"""Event engine — pulls each configured RTSP stream, runs MOG2 background
subtraction at a downsampled FPS, and emits an "event" (3 snapshots + DB
row) when continuous motion is detected.

Design notes
------------
* One worker thread per channel, each owns its own VideoCapture and MOG2.
* A small ring buffer (the last `pre_event_sec * fps` frames) is kept so that
  when an event fires, we can grab a "before" snapshot in addition to the
  trigger frame itself. The "after" snapshot is captured `post_event_sec`
  later in the same loop.
* DB writes are serialised through a single worker thread + WAL mode, so
  writes from different channels don't collide.
* Per-channel cooldown prevents flapping (one event per 30 s by default).
* On RTSP read failure the worker exponentially backs off and reconnects.

Triggers a real event by writing a row with status='pending'; vlm_worker
will pick it up.
"""

from __future__ import annotations

import json
import os
import sys
import threading
import time
import uuid
from collections import deque
from pathlib import Path

import cv2
import numpy as np

from common import (
    ensure_db,
    ensure_dir,
    load_config,
    now_ms,
    open_db,
    setup_logging,
    snapshot_dir_for,
)


# ─── DB worker (serialises writes) ─────────────────────────────────────
class DbWriter(threading.Thread):
    def __init__(self, db_path: str, logger):
        super().__init__(name="DbWriter", daemon=True)
        self.db_path = db_path
        self.log = logger
        self.queue: deque = deque()
        self.cv = threading.Condition()
        self._stop = threading.Event()

    def submit(self, sql: str, params: tuple) -> None:
        with self.cv:
            self.queue.append((sql, params))
            self.cv.notify()

    def stop(self) -> None:
        self._stop.set()
        with self.cv:
            self.cv.notify_all()

    def run(self) -> None:
        conn = open_db(self.db_path)
        try:
            while not self._stop.is_set():
                with self.cv:
                    while not self.queue and not self._stop.is_set():
                        self.cv.wait(timeout=1.0)
                    items = list(self.queue)
                    self.queue.clear()
                for sql, params in items:
                    try:
                        conn.execute(sql, params)
                    except Exception as e:
                        self.log.exception("DB write failed: %s", e)
        finally:
            try:
                conn.close()
            except Exception:
                pass


# ─── Channel worker ────────────────────────────────────────────────────
class ChannelWorker(threading.Thread):
    def __init__(self, channel_cfg: dict, cfg: dict, db_writer: DbWriter, logger):
        super().__init__(name=f"ch-{channel_cfg['id']}", daemon=True)
        self.channel = channel_cfg["id"]
        self.rtsp = channel_cfg["rtsp"]
        self.cfg = cfg
        self.db = db_writer
        self.log = logger
        self._stop = threading.Event()

        m = cfg["motion"]
        self.target_fps = max(1, int(m.get("fps", 5)))
        self.min_area_ratio = float(m.get("min_area_ratio", 0.01))
        self.trigger_frames = int(m.get("trigger_frames", 3))
        self.cooldown_sec = float(m.get("cooldown_sec", 30))
        self.warmup_frames = int(m.get("warmup_frames", 30))
        self.blur_kernel = int(m.get("blur_kernel", 5))
        if self.blur_kernel % 2 == 0:
            self.blur_kernel += 1

        s = cfg["storage"]
        self.snapshot_root = ensure_dir(s["snapshot_dir"])
        self.pre_sec = float(s.get("pre_event_sec", 1))
        self.post_sec = float(s.get("post_event_sec", 4))
        self.jpeg_quality = int(s.get("jpeg_quality", 85))

        # ring buffer of (ts_ms, frame) for the last `pre_sec` seconds
        self._buf_capacity = max(1, int(self.pre_sec * self.target_fps) + 1)
        self.last_trigger_ts: float = 0.0

    def stop(self) -> None:
        self._stop.set()

    # ── helpers ──
    def _open_capture(self) -> cv2.VideoCapture | None:
        os.environ.setdefault(
            "OPENCV_FFMPEG_CAPTURE_OPTIONS",
            "rtsp_transport;tcp|stimeout;5000000",
        )
        cap = cv2.VideoCapture(self.rtsp, cv2.CAP_FFMPEG)
        try:
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        except Exception:
            pass
        if not cap.isOpened():
            cap.release()
            return None
        return cap

    def _save_jpg(self, frame: np.ndarray, dst: Path) -> None:
        cv2.imwrite(str(dst), frame, [int(cv2.IMWRITE_JPEG_QUALITY), self.jpeg_quality])

    def _emit_event(
        self,
        trigger_frame: np.ndarray,
        pre_frame: np.ndarray | None,
        ratio: float,
        cap: cv2.VideoCapture,
    ) -> None:
        """Capture 3 snapshots (pre, trigger, post) and queue a DB insert."""
        ts = now_ms()
        eid = uuid.uuid4().hex
        evt_dir = snapshot_dir_for(self.snapshot_root, eid)

        snapshots: list[str] = []

        idx = 0
        if pre_frame is not None:
            p = evt_dir / f"{idx}_pre.jpg"
            self._save_jpg(pre_frame, p)
            snapshots.append(str(p.relative_to(self.snapshot_root)))
            idx += 1

        p = evt_dir / f"{idx}_trigger.jpg"
        self._save_jpg(trigger_frame, p)
        snapshots.append(str(p.relative_to(self.snapshot_root)))
        idx += 1

        # capture post-frame ~post_sec later (best-effort, single read)
        post_deadline = time.monotonic() + self.post_sec
        post_frame: np.ndarray | None = None
        while time.monotonic() < post_deadline and not self._stop.is_set():
            ok, f = cap.read()
            if ok and f is not None:
                post_frame = f
            time.sleep(1.0 / self.target_fps)
        if post_frame is not None:
            p = evt_dir / f"{idx}_post.jpg"
            self._save_jpg(post_frame, p)
            snapshots.append(str(p.relative_to(self.snapshot_root)))

        clip_start = ts - int(self.pre_sec * 1000)
        clip_end = ts + int(self.post_sec * 1000)

        self.db.submit(
            """INSERT INTO events
                (id, ts, channel, source, snapshots, clip_start, clip_end,
                 status, raw_motion, created_at)
               VALUES (?, ?, ?, 'motion', ?, ?, ?, 'pending', ?, ?)""",
            (eid, ts, self.channel, json.dumps(snapshots),
             clip_start, clip_end, ratio, ts),
        )
        self.log.info(
            "triggered ch=%s id=%s ratio=%.4f snapshots=%d",
            self.channel, eid, ratio, len(snapshots),
        )

    # ── main loop ──
    def run(self) -> None:
        backoff = 1.0
        while not self._stop.is_set():
            cap = self._open_capture()
            if cap is None:
                self.log.warning("ch=%s open failed, retry in %.1fs", self.channel, backoff)
                self._stop.wait(backoff)
                backoff = min(backoff * 2, 30.0)
                continue
            self.log.info("ch=%s connected to %s", self.channel, self.rtsp)
            backoff = 1.0

            mog = cv2.createBackgroundSubtractorMOG2(
                history=200, varThreshold=25, detectShadows=False
            )
            ring: deque = deque(maxlen=self._buf_capacity)
            consecutive = 0
            frame_no = 0
            frame_interval = 1.0 / self.target_fps
            next_t = time.monotonic()

            try:
                while not self._stop.is_set():
                    ok, frame = cap.read()
                    if not ok or frame is None:
                        self.log.warning("ch=%s read failed, reconnecting", self.channel)
                        break

                    # Throttle the heavy work to target_fps; we still need to
                    # consume frames from the buffer to avoid stale streams.
                    now = time.monotonic()
                    if now < next_t:
                        continue
                    next_t = now + frame_interval

                    frame_no += 1
                    ring.append((now_ms(), frame))

                    if frame_no <= self.warmup_frames:
                        mog.apply(cv2.GaussianBlur(frame,
                                                   (self.blur_kernel, self.blur_kernel), 0))
                        continue

                    blurred = cv2.GaussianBlur(
                        frame, (self.blur_kernel, self.blur_kernel), 0
                    )
                    fg = mog.apply(blurred)
                    # binarise to suppress shadow grey
                    _, fg = cv2.threshold(fg, 200, 255, cv2.THRESH_BINARY)
                    fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))

                    h, w = fg.shape[:2]
                    ratio = float(np.count_nonzero(fg)) / float(h * w)

                    if ratio >= self.min_area_ratio:
                        consecutive += 1
                    else:
                        consecutive = 0
                        continue

                    if consecutive < self.trigger_frames:
                        continue

                    # cooldown gate
                    now_s = time.monotonic()
                    if now_s - self.last_trigger_ts < self.cooldown_sec:
                        consecutive = 0
                        continue
                    self.last_trigger_ts = now_s
                    consecutive = 0

                    pre_frame = ring[0][1] if len(ring) > 1 else None
                    self._emit_event(frame, pre_frame, ratio, cap)
            finally:
                try:
                    cap.release()
                except Exception:
                    pass

        self.log.info("ch=%s worker stopped", self.channel)


# ─── main ──────────────────────────────────────────────────────────────
def main() -> int:
    cfg = load_config()
    log = setup_logging("event_engine", cfg)

    db_path = cfg["storage"]["db_path"]
    ensure_db(db_path)
    log.info("db ready at %s", db_path)

    db_writer = DbWriter(db_path, log)
    db_writer.start()

    workers: list[ChannelWorker] = []
    for ch in cfg["channels"]:
        w = ChannelWorker(ch, cfg, db_writer, log)
        w.start()
        workers.append(w)
    log.info("started %d channel workers", len(workers))

    stop_evt = threading.Event()

    def _shutdown(*_args):
        log.info("shutdown requested")
        stop_evt.set()

    import signal
    for s in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(s, _shutdown)
        except (ValueError, OSError):
            pass

    while not stop_evt.is_set():
        time.sleep(1.0)

    for w in workers:
        w.stop()
    for w in workers:
        w.join(timeout=5)
    db_writer.stop()
    db_writer.join(timeout=5)
    log.info("event_engine exited cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
