# ai/feishu_notify.py（示意）
import requests, sys, json

WEBHOOK = "https://open.feishu.cn/open-apis/bot/v2/hook/ddb19631-2577-4327-bcd5-b71734875991"

def send_markdown(title: str, body: str):
    resp = requests.post(WEBHOOK, json={
        "msg_type": "interactive",
        "card": {
            "header": {"title": {"tag": "plain_text", "content": title}},
            "elements": [{"tag": "markdown", "content": body}]
        }
    }, timeout=10)
    resp.raise_for_status()

if __name__ == "__main__":
    send_markdown(sys.argv[1], sys.stdin.read())