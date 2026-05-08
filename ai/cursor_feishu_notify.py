"""Feishu (Lark) custom-bot notifier.

Resolves the webhook URL in this order:
  1. Env var  FEISHU_WEBHOOK
  2. File     ai/feishu.yaml  (key: webhook, gitignored)
  3. Abort

Usage as a CLI
--------------
# body from stdin, markdown supported:
echo "- 一条**重要**事件\n- 另一条事件" | \
    python cursor_feishu_notify.py --title "AGX 日报 2026-04-26"

# body from a file:
python cursor_feishu_notify.py --title "AGX 日报" --body-file /tmp/report.md

# dry-run (print the payload, do not POST):
python cursor_feishu_notify.py --title t --body "hi" --dry-run

Usage as a library
------------------
from cursor_feishu_notify import send_card, send_markdown
send_markdown("标题", "**加粗**正文")
send_card("标题", elements=[...])              # full control

Exit codes
----------
0 success · 2 config error · 3 HTTP/Feishu error
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any

import requests


CONFIG_FILE = Path(__file__).with_name("feishu.yaml")


def _read_config_webhook() -> str | None:
    if not CONFIG_FILE.exists():
        return None
    try:
        import yaml  # Lazy import; PyYAML is already in requirements.txt
    except ImportError:
        return None
    try:
        data = yaml.safe_load(CONFIG_FILE.read_text(encoding="utf-8")) or {}
    except Exception:
        return None
    url = data.get("webhook")
    return url.strip() if isinstance(url, str) and url.strip() else None


def resolve_webhook() -> str:
    env = os.environ.get("FEISHU_WEBHOOK", "").strip()
    if env:
        return env
    cfg = _read_config_webhook()
    if cfg:
        return cfg
    raise SystemExit(
        "Feishu webhook not configured. Set env FEISHU_WEBHOOK "
        f"or create {CONFIG_FILE} with a `webhook: <url>` key."
    )


def _post(payload: dict[str, Any], timeout: int, dry_run: bool) -> dict[str, Any]:
    if dry_run:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return {"code": 0, "msg": "dry-run"}
    url = resolve_webhook()
    r = requests.post(url, json=payload, timeout=timeout)
    r.raise_for_status()
    data = r.json()
    if data.get("code") not in (0, None):
        raise RuntimeError(f"Feishu rejected: {data}")
    return data


def send_card(
    title: str,
    elements: list[dict[str, Any]],
    template: str = "blue",
    timeout: int = 10,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Send an interactive card with caller-supplied elements."""
    payload = {
        "msg_type": "interactive",
        "card": {
            "config": {"wide_screen_mode": True},
            "header": {
                "template": template,
                "title": {"tag": "plain_text", "content": title},
            },
            "elements": elements,
        },
    }
    return _post(payload, timeout, dry_run)


def send_markdown(
    title: str,
    body: str,
    template: str = "blue",
    timeout: int = 10,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Send a single-element markdown card (convenience wrapper)."""
    return send_card(
        title=title,
        elements=[{"tag": "markdown", "content": body}],
        template=template,
        timeout=timeout,
        dry_run=dry_run,
    )


def _cli() -> int:
    p = argparse.ArgumentParser(description="Push a card to a Feishu custom bot.")
    p.add_argument("--title", required=True, help="Card header title.")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--body", help="Markdown body (inline).")
    g.add_argument("--body-file", help="Markdown body file path. Use '-' for stdin.")
    p.add_argument(
        "--template", default="blue",
        choices=["blue", "green", "grey", "orange", "red", "turquoise", "purple"],
        help="Card header colour.",
    )
    p.add_argument("--timeout", type=int, default=10)
    p.add_argument("--dry-run", action="store_true", help="Print payload, don't POST.")
    args = p.parse_args()

    if args.body is not None:
        body = args.body
    elif args.body_file == "-" or (args.body_file is None and not sys.stdin.isatty()):
        body = sys.stdin.read()
    elif args.body_file:
        body = Path(args.body_file).read_text(encoding="utf-8")
    else:
        print("error: provide --body, --body-file, or pipe to stdin", file=sys.stderr)
        return 2

    try:
        resp = send_markdown(
            args.title, body, template=args.template,
            timeout=args.timeout, dry_run=args.dry_run,
        )
    except SystemExit:
        raise
    except (requests.RequestException, RuntimeError) as e:
        print(f"feishu send failed: {e}", file=sys.stderr)
        return 3

    if not args.dry_run:
        print(f"ok: {resp}")
    return 0


if __name__ == "__main__":
    sys.exit(_cli())
