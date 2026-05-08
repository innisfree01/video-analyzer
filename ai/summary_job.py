"""Daily summary job — uses Qwen2.5-7B (via Ollama) to digest the events
of a given day into a human-readable highlights list and a free-form
narrative paragraph, then UPSERTs the result into daily_summary.

Two run modes
-------------
1. One-shot CLI (used by cron or manual ops):
       python summary_job.py --date 2026-04-09
   If --date is omitted, defaults to today.

2. Daemon with APScheduler (used by ops who don't want a system cron):
       python summary_job.py --daemon
   Runs at 23:30 every day (local time).

Both modes share the same `generate_summary(date)` function.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from typing import Any

import requests

from common import (
    ensure_db,
    load_config,
    now_ms,
    open_db,
    setup_logging,
)
from cursor_feishu_notify import CONFIG_FILE as FEISHU_CFG_FILE
from cursor_feishu_notify import send_markdown as _feishu_send_markdown


SYSTEM_PROMPT = (
    "你是一个安防系统的日报生成助手。"
    "输出必须是合法 JSON，不要 markdown 代码块、不要解释文字。"
)

USER_PROMPT_TPL = """以下是 {date} 这一天监控系统记录的 {n_events} 条已分析事件（按时间排序）：

{events_block}

请生成一份当日安防日报，严格按以下 JSON 返回：

{{
  "highlights": [
    {{"time":"HH:MM","channel":"camX","summary":"一句话概括","severity":"low|medium|high"}}
    // 5-10 条最重要的事件，按 severity 降序，再按时间排序
  ],
  "narrative": "用 3-5 句中文写当天整体情况：异常时段、活跃通道、有无可疑事件、给值守人员的建议。"
}}
"""

JSON_RE = re.compile(r"\{.*\}", re.DOTALL)


SEVERITY_EMOJI = {"high": "🔴", "medium": "🟠", "low": "🟢"}


def _feishu_enabled() -> bool:
    """Push to Feishu if env var set OR ai/feishu.yaml has enable_summary_push: true."""
    import os
    if os.environ.get("FEISHU_WEBHOOK", "").strip():
        return True
    if not FEISHU_CFG_FILE.exists():
        return False
    try:
        import yaml
        data = yaml.safe_load(FEISHU_CFG_FILE.read_text(encoding="utf-8")) or {}
    except Exception:
        return False
    return bool(data.get("webhook")) and bool(data.get("enable_summary_push", True))


def _render_summary_markdown(payload: dict) -> str:
    """Turn a summary payload into a compact Feishu-markdown body."""
    lines = [
        f"**日期**：{payload['date']}",
        f"**事件总数**：{payload['total_events']}"
        f"（🔴{payload['high_count']} · 🟠{payload['medium_count']} · 🟢{payload['low_count']}）",
    ]
    chan_stats = payload.get("channel_stats") or {}
    if chan_stats:
        lines.append(
            "**通道活跃度**：" + "，".join(
                f"{k}={v}" for k, v in sorted(chan_stats.items())
            )
        )
    narrative = (payload.get("narrative") or "").strip()
    if narrative:
        lines += ["", "**整体情况**", narrative]

    hls = payload.get("highlights") or []
    if hls:
        lines += ["", "**重要事件**"]
        for h in hls[:10]:
            emoji = SEVERITY_EMOJI.get(h.get("severity", "low"), "⚪")
            lines.append(
                f"- {emoji} `{h.get('time','')}` **{h.get('channel','')}** — {h.get('summary','')}"
            )
    lines += ["", f"_model: {payload.get('model','?')}_"]
    return "\n".join(lines)


def _push_to_feishu(payload: dict, log) -> None:
    try:
        body = _render_summary_markdown(payload)
        title = f"AGX 安防日报 · {payload['date']}"
        template = "red" if payload.get("high_count", 0) > 0 else "blue"
        _feishu_send_markdown(title, body, template=template, timeout=10)
        log.info("feishu summary pushed (date=%s)", payload["date"])
    except Exception as e:
        log.warning("feishu push failed (non-fatal): %s", e)


def _date_to_window_ms(date_str: str) -> tuple[int, int]:
    t = time.strptime(date_str, "%Y-%m-%d")
    start = int(time.mktime(t) * 1000)
    return start, start + 24 * 3600 * 1000


def _format_events_for_prompt(events: list[dict], max_chars: int = 6000) -> str:
    """Compact textual list. Truncates if too many events."""
    lines = []
    for e in events:
        hhmm = time.strftime("%H:%M", time.localtime(e["ts"] / 1000))
        desc = (e.get("description") or "").replace("\n", " ")
        sev = e.get("severity") or "?"
        lines.append(f"- [{hhmm}] {e['channel']} ({sev}): {desc}")
        if sum(len(x) for x in lines) > max_chars:
            lines.append(f"... (后续 {len(events) - len(lines) + 1} 条略)")
            break
    return "\n".join(lines)


def _call_llm(url: str, model: str, prompt: str, timeout: int) -> str:
    r = requests.post(
        f"{url.rstrip('/')}/api/chat",
        json={
            "model": model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": prompt},
            ],
            "stream": False,
            "options": {"temperature": 0.3, "num_predict": 1024},
        },
        timeout=timeout,
    )
    r.raise_for_status()
    return r.json().get("message", {}).get("content", "")


def _parse_llm_json(raw: str) -> dict | None:
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        pass
    m = JSON_RE.search(raw)
    if not m:
        return None
    try:
        return json.loads(m.group(0))
    except json.JSONDecodeError:
        return None


def _normalise_highlight(h: Any) -> dict | None:
    if not isinstance(h, dict):
        return None
    sev = str(h.get("severity", "low")).lower()
    if sev not in ("low", "medium", "high"):
        sev = "low"
    return {
        "time": str(h.get("time", "")),
        "channel": str(h.get("channel", "")),
        "summary": str(h.get("summary", "")).strip(),
        "severity": sev,
    }


def generate_summary(date: str, log) -> dict:
    cfg = load_config()
    db_path = cfg["storage"]["db_path"]
    ensure_db(db_path)
    ollama = cfg["ollama"]

    start_ms, end_ms = _date_to_window_ms(date)
    conn = open_db(db_path)
    try:
        rows = conn.execute(
            """SELECT ts, channel, severity, description
               FROM events
               WHERE ts >= ? AND ts < ?
               ORDER BY ts ASC""",
            (start_ms, end_ms),
        ).fetchall()
    finally:
        conn.close()

    events = [dict(r) for r in rows]
    log.info("date=%s loaded %d events", date, len(events))

    high = sum(1 for e in events if e.get("severity") == "high")
    medium = sum(1 for e in events if e.get("severity") == "medium")
    low = sum(1 for e in events if e.get("severity") == "low")
    chan_stats: dict[str, int] = {}
    for e in events:
        chan_stats[e["channel"]] = chan_stats.get(e["channel"], 0) + 1

    highlights: list[dict] = []
    narrative = ""
    used_model = ollama["llm_model"]

    if not events:
        narrative = f"{date} 没有任何事件记录，监控系统全天平静。"
    else:
        try:
            prompt = USER_PROMPT_TPL.format(
                date=date,
                n_events=len(events),
                events_block=_format_events_for_prompt(events),
            )
            raw = _call_llm(
                ollama["url"], used_model, prompt, int(ollama["timeout_sec"])
            )
            parsed = _parse_llm_json(raw)
            if parsed:
                hl_in = parsed.get("highlights") or []
                if isinstance(hl_in, list):
                    highlights = [
                        h for h in (
                            _normalise_highlight(x) for x in hl_in
                        ) if h is not None
                    ][:10]
                narrative = str(parsed.get("narrative", "")).strip()
            else:
                log.warning("LLM produced unparseable output: %r", raw[:200])
                narrative = "[日报生成失败：模型输出无法解析]"
        except Exception as e:
            log.exception("LLM call failed: %s", e)
            narrative = f"[日报生成失败：{e}]"

    payload = {
        "date": date,
        "total_events": len(events),
        "high_count": high,
        "medium_count": medium,
        "low_count": low,
        "highlights": highlights,
        "channel_stats": chan_stats,
        "narrative": narrative,
        "generated_at": now_ms(),
        "model": used_model,
    }

    conn = open_db(db_path)
    try:
        conn.execute(
            """INSERT INTO daily_summary
                (date, total_events, high_count, medium_count, low_count,
                 highlights, channel_stats, narrative, generated_at, model)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT(date) DO UPDATE SET
                 total_events  = excluded.total_events,
                 high_count    = excluded.high_count,
                 medium_count  = excluded.medium_count,
                 low_count     = excluded.low_count,
                 highlights    = excluded.highlights,
                 channel_stats = excluded.channel_stats,
                 narrative     = excluded.narrative,
                 generated_at  = excluded.generated_at,
                 model         = excluded.model""",
            (
                payload["date"], payload["total_events"],
                payload["high_count"], payload["medium_count"], payload["low_count"],
                json.dumps(payload["highlights"], ensure_ascii=False),
                json.dumps(payload["channel_stats"], ensure_ascii=False),
                payload["narrative"], payload["generated_at"], payload["model"],
            ),
        )
    finally:
        conn.close()

    log.info(
        "wrote summary date=%s total=%d high=%d medium=%d low=%d hl=%d",
        date, payload["total_events"], high, medium, low, len(highlights),
    )

    if _feishu_enabled():
        _push_to_feishu(payload, log)

    return payload


# ─── Daemon mode (APScheduler) ─────────────────────────────────────────
def run_daemon(log) -> int:
    from apscheduler.schedulers.blocking import BlockingScheduler
    from apscheduler.triggers.cron import CronTrigger

    sched = BlockingScheduler(timezone="Asia/Shanghai")

    def _job():
        # Summarise the day that just finished, regardless of clock skew.
        d = time.strftime("%Y-%m-%d", time.localtime())
        log.info("scheduled run for date=%s", d)
        try:
            generate_summary(d, log)
        except Exception as e:
            log.exception("scheduled summary failed: %s", e)

    sched.add_job(_job, CronTrigger(hour=23, minute=30), id="daily_summary")
    log.info("summary_job daemon started, next run 23:30 daily (Asia/Shanghai)")
    try:
        sched.start()
    except (KeyboardInterrupt, SystemExit):
        log.info("daemon stopped")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Daily summary generator")
    parser.add_argument(
        "--date", help="YYYY-MM-DD (local time). Default: today.")
    parser.add_argument(
        "--daemon", action="store_true", help="Run as long-lived APScheduler.")
    args = parser.parse_args()

    cfg = load_config()
    log = setup_logging("summary_job", cfg)

    if args.daemon:
        return run_daemon(log)

    date = args.date or time.strftime("%Y-%m-%d", time.localtime())
    try:
        time.strptime(date, "%Y-%m-%d")
    except ValueError:
        print(f"Invalid date: {date} (expected YYYY-MM-DD)", file=sys.stderr)
        return 2

    payload = generate_summary(date, log)
    print(json.dumps(
        {k: payload[k] for k in (
            "date", "total_events", "high_count", "medium_count", "low_count",
            "channel_stats", "highlights", "narrative", "model",
        )},
        ensure_ascii=False, indent=2,
    ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
