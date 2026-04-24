"""VLM worker — polls events with status='pending', sends snapshots to a
local Qwen2.5-VL model via Ollama, parses the JSON response and writes
description / severity / tags back into the events table.

Why Ollama?
-----------
The HTTP API is trivial, the binary handles model loading + GPU offloading
on Jetson via CUDA, and switching the model later (e.g. to MiniCPM-V) is a
one-line change in config.yaml.

Robustness
----------
* If Ollama is down or the request times out → status stays 'pending';
  next poll retries (this is intentional, transient errors should self-heal).
* If JSON parsing fails after the request succeeds → status='failed' so we
  don't loop forever on a poisoned event.
* Ratio-style truncation: only `vlm_batch_size` events processed per poll.
"""

from __future__ import annotations

import base64
import json
import re
import signal
import sys
import time
from pathlib import Path

import requests

from common import (
    ensure_db,
    load_config,
    now_ms,
    open_db,
    setup_logging,
)


SYSTEM_PROMPT = (
    "你是一个专业的安防视频分析助手，输出必须是合法 JSON，不要任何额外文字。"
)

USER_PROMPT_TPL = """下面是来自监控通道 {channel} 在 {time_str} 触发告警时的 {n_imgs} 张连续画面。

请完成：
1. 用 1-2 句中文描述画面中正在发生什么。
2. 判断告警重要性等级 severity:
   - high  : 涉及人员入侵、可疑行为或明显异常
   - medium: 有明显活动但无明确威胁（送货、巡视、家人出入等）
   - low   : 环境扰动（光线、宠物、风吹植物、阴影变化）
3. 提取 1-5 个关键标签（中文短词）。

严格按以下 JSON 返回，不要 markdown 代码块、不要解释：
{{"description":"...","severity":"low|medium|high","tags":["...","..."]}}
"""


JSON_RE = re.compile(r"\{.*\}", re.DOTALL)


class OllamaClient:
    def __init__(self, url: str, timeout: int):
        self.url = url.rstrip("/")
        self.timeout = timeout

    def generate_vision(
        self,
        model: str,
        prompt: str,
        images_b64: list[str],
        system: str | None = None,
    ) -> str:
        payload = {
            "model": model,
            "prompt": prompt,
            "images": images_b64,
            "stream": False,
            "options": {
                "temperature": 0.2,
                "num_predict": 256,
            },
        }
        if system:
            payload["system"] = system
        r = requests.post(
            f"{self.url}/api/generate", json=payload, timeout=self.timeout
        )
        r.raise_for_status()
        return r.json().get("response", "")


def encode_image(path: Path) -> str | None:
    try:
        with open(path, "rb") as f:
            return base64.b64encode(f.read()).decode("ascii")
    except FileNotFoundError:
        return None


def parse_vlm_json(raw: str) -> dict | None:
    raw = raw.strip()
    if not raw:
        return None
    # try direct parse first
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        pass
    # otherwise extract the largest {...} block
    m = JSON_RE.search(raw)
    if not m:
        return None
    try:
        return json.loads(m.group(0))
    except json.JSONDecodeError:
        return None


def normalise_result(obj: dict) -> dict:
    severity = str(obj.get("severity", "low")).lower().strip()
    if severity not in ("low", "medium", "high"):
        severity = "low"

    tags = obj.get("tags") or []
    if isinstance(tags, str):
        tags = [tags]
    tags = [str(t).strip() for t in tags if str(t).strip()][:5]

    description = str(obj.get("description", "")).strip() or "无描述"
    return {"description": description, "severity": severity, "tags": tags}


def fetch_pending(conn, limit: int) -> list:
    cur = conn.execute(
        "SELECT id, ts, channel, snapshots FROM events "
        "WHERE status='pending' ORDER BY ts ASC LIMIT ?",
        (limit,),
    )
    return cur.fetchall()


def update_analyzed(conn, event_id: str, result: dict) -> None:
    conn.execute(
        """UPDATE events SET
              description=?, severity=?, tags=?,
              status='analyzed', analyzed_at=?
           WHERE id=?""",
        (result["description"], result["severity"],
         json.dumps(result["tags"], ensure_ascii=False),
         now_ms(), event_id),
    )


def mark_failed(conn, event_id: str, reason: str) -> None:
    conn.execute(
        """UPDATE events SET
              status='failed', description=?, analyzed_at=?
           WHERE id=?""",
        (f"[VLM 解析失败] {reason}"[:500], now_ms(), event_id),
    )


def process_event(
    row,
    snapshot_root: Path,
    ollama: OllamaClient,
    model: str,
    log,
) -> tuple[str, dict | str]:
    """Returns ('analyzed', result_dict) or ('failed', reason)
    or ('skip', reason) — skip means transient, leave pending."""
    eid = row["id"]
    channel = row["channel"]
    ts = row["ts"]
    try:
        rel_paths = json.loads(row["snapshots"] or "[]")
    except json.JSONDecodeError:
        return "failed", "snapshots field is not JSON"

    images_b64: list[str] = []
    for rp in rel_paths:
        b64 = encode_image(snapshot_root / rp)
        if b64:
            images_b64.append(b64)
    if not images_b64:
        return "failed", "no readable snapshot files"

    time_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts / 1000))
    user_prompt = USER_PROMPT_TPL.format(
        channel=channel, time_str=time_str, n_imgs=len(images_b64)
    )

    try:
        raw = ollama.generate_vision(
            model=model,
            prompt=user_prompt,
            images_b64=images_b64,
            system=SYSTEM_PROMPT,
        )
    except requests.RequestException as e:
        log.warning("ollama transient error for %s: %s", eid, e)
        return "skip", str(e)

    parsed = parse_vlm_json(raw)
    if parsed is None:
        log.warning("VLM produced unparseable output for %s: %r", eid, raw[:200])
        return "failed", f"unparseable: {raw[:120]}"

    return "analyzed", normalise_result(parsed)


def warmup(ollama: OllamaClient, model: str, log) -> None:
    """Send a tiny request so the model is paged into VRAM before real events."""
    try:
        # 1×1 black png base64
        tiny = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4n"
                "GP4//8/AwAI/AL+XJGKVQAAAABJRU5ErkJggg==")
        ollama.generate_vision(model=model, prompt="ok", images_b64=[tiny])
        log.info("VLM warmup ok (model=%s)", model)
    except Exception as e:
        log.warning("VLM warmup failed (model=%s): %s", model, e)


def main() -> int:
    cfg = load_config()
    log = setup_logging("vlm_worker", cfg)

    db_path = cfg["storage"]["db_path"]
    snapshot_root = Path(cfg["storage"]["snapshot_dir"])
    ensure_db(db_path)

    ollama_cfg = cfg["ollama"]
    ollama = OllamaClient(ollama_cfg["url"], int(ollama_cfg["timeout_sec"]))
    model = ollama_cfg["vlm_model"]
    poll_interval = float(ollama_cfg.get("vlm_poll_interval_sec", 2.0))
    batch_size = int(ollama_cfg.get("vlm_batch_size", 5))

    warmup(ollama, model, log)

    stop = False

    def _shutdown(*_):
        nonlocal stop
        log.info("shutdown requested")
        stop = True

    for s in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(s, _shutdown)
        except (ValueError, OSError):
            pass

    log.info("vlm_worker started, polling every %.1fs (batch=%d)",
             poll_interval, batch_size)

    conn = open_db(db_path)
    try:
        while not stop:
            try:
                rows = fetch_pending(conn, batch_size)
            except Exception as e:
                log.exception("fetch_pending failed: %s", e)
                time.sleep(poll_interval)
                continue

            if not rows:
                time.sleep(poll_interval)
                continue

            for row in rows:
                if stop:
                    break
                t0 = time.time()
                status, payload = process_event(
                    row, snapshot_root, ollama, model, log
                )
                dt = time.time() - t0
                if status == "analyzed":
                    update_analyzed(conn, row["id"], payload)  # type: ignore[arg-type]
                    log.info(
                        "analyzed id=%s ch=%s severity=%s in %.2fs",
                        row["id"], row["channel"], payload["severity"], dt,  # type: ignore[index]
                    )
                elif status == "failed":
                    mark_failed(conn, row["id"], payload)  # type: ignore[arg-type]
                else:  # skip → leave pending
                    log.info("skip id=%s (will retry): %s", row["id"], payload)
                    time.sleep(min(poll_interval * 2, 5.0))
                    break  # break batch to avoid hammering ollama
    finally:
        conn.close()

    log.info("vlm_worker exited cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
