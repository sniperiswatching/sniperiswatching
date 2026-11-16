"""Local bridge for the ESP32-S3 ChatGPT quota display for 0.96 inch OLED.

The Edge extension posts the visible Usage-page snapshot here. The ESP32
reads the normalized JSON from /usage over the same trusted LAN.
"""

from __future__ import annotations

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Any

BASE_DIR = Path(__file__).resolve().parent
TASKS_FILE = BASE_DIR / "tasks.json"
HOST = "0.0.0.0"
PORT = 8765
MAX_PAYLOAD_BYTES = 8192

DEFAULT_TASKS = [
    {
        "title": "暂无任务",
        "detail": "请在电脑端任务管理页添加任务",
        "status": "working",
    },
    {
        "title": "暂无任务",
        "detail": "等待第二个最近任务",
        "status": "working",
    },
    {
        "title": "暂无任务",
        "detail": "等待第三个最近任务",
        "status": "working",
    },
]
VALID_STATUSES = {"completed", "working", "attention"}

_latest_usage: dict[str, Any] = {}
_lock = threading.Lock()


def clean_tasks(items: Any) -> list[dict[str, str]]:
    """Return exactly three bounded task records for the TFT."""
    if not isinstance(items, list):
        items = []

    cleaned: list[dict[str, str]] = []
    for raw in items[:3]:
        item = raw if isinstance(raw, dict) else {}
        status = str(item.get("status", "working"))
        if status not in VALID_STATUSES:
            status = "attention"
        title = str(item.get("title", "未命名任务")).strip()[:48]
        detail = str(item.get("detail", "")).strip()[:120]
        cleaned.append(
            {
                "title": title or "未命名任务",
                "detail": detail,
                "status": status,
            }
        )

    while len(cleaned) < 3:
        cleaned.append(dict(DEFAULT_TASKS[len(cleaned)]))
    return cleaned


def read_tasks() -> list[dict[str, str]]:
    try:
        return clean_tasks(json.loads(TASKS_FILE.read_text(encoding="utf-8")))
    except (OSError, json.JSONDecodeError, TypeError):
        return clean_tasks(DEFAULT_TASKS)


def write_tasks(items: Any) -> list[dict[str, str]]:
    cleaned = clean_tasks(items)
    TASKS_FILE.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(
        "w", encoding="utf-8", dir=TASKS_FILE.parent, delete=False
    ) as temp:
        json.dump(cleaned, temp, ensure_ascii=False, indent=2)
        temp.write("\n")
        temp_path = Path(temp.name)
    temp_path.replace(TASKS_FILE)
    return cleaned


def snapshot() -> dict[str, Any]:
    with _lock:
        data = dict(_latest_usage)
    data["tasks"] = read_tasks()
    received_at = data.pop("_received_at", None)
    if received_at is None:
        return {
            "valid": False,
            "age_seconds": 999999,
            "tasks": data["tasks"],
        }
    data["age_seconds"] = max(0, int(time.time() - received_at))
    return data


MANAGE_HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ChatGPT 额度摆件 · 最近三个任务</title>
  <style>
    :root { color-scheme: dark; }
    * { box-sizing: border-box; }
    body { margin: 0 auto; max-width: 820px; padding: 28px 18px 48px; background: #071018; color: #edf7fb; font-family: system-ui, "Microsoft YaHei", sans-serif; }
    h1 { margin: 0 0 6px; font-size: 24px; }
    .hint { margin: 0 0 22px; color: #a7bcc7; line-height: 1.65; }
    .task { margin: 14px 0; padding: 16px; border: 1px solid #274453; border-radius: 14px; background: #0d1d27; }
    .order { color: #80a5b4; font-size: 13px; margin-bottom: 6px; }
    label { display: block; margin: 10px 0 5px; color: #b8cbd4; font-size: 13px; }
    input, textarea, select { width: 100%; padding: 10px 12px; color: #f7fbfd; background: #061018; border: 1px solid #315160; border-radius: 9px; font: inherit; }
    textarea { min-height: 66px; resize: vertical; }
    select { max-width: 300px; }
    button { margin-top: 8px; padding: 12px 22px; color: #001a10; background: #38e18a; border: 0; border-radius: 10px; font-weight: 700; cursor: pointer; }
    button:hover { background: #6af3aa; }
    #msg { margin-left: 12px; color: #77e8ab; }
    code { color: #6bd6ff; }
  </style>
</head>
<body>
  <h1>ChatGPT 额度摆件</h1>
  <p class="hint">按最近更新时间填写三项不同任务。绿色表示已完成，黄色表示工作中，红色表示有错误或需要决策。保存后 ESP32 会在下一次轮询时刷新。</p>
  <div id="items"></div>
  <button id="save">保存三个任务</button><span id="msg"></span>
  <script>
    const defaults = [
      {title: "暂无任务", detail: "请在电脑端任务管理页添加任务", status: "working"},
      {title: "暂无任务", detail: "等待第二个最近任务", status: "working"},
      {title: "暂无任务", detail: "等待第三个最近任务", status: "working"}
    ];
    let tasks = defaults;
    const escapeHtml = value => String(value ?? "").replace(/[&<>\"']/g, ch => ({"&":"&amp;", "<":"&lt;", ">":"&gt;", "\"":"&quot;", "'":"&#39;"}[ch]));
    function render() {
      const labels = ["最新任务（最近更新）", "第二新任务", "第三新任务（列表中最旧）"];
      document.getElementById("items").innerHTML = tasks.map((task, index) => `
        <section class="task">
          <div class="order">窗口 ${index + 1} · ${labels[index]}</div>
          <label for="title${index}">任务名称</label>
          <input id="title${index}" maxlength="48" value="${escapeHtml(task.title)}">
          <label for="detail${index}">工作内容 / 说明</label>
          <textarea id="detail${index}" maxlength="120">${escapeHtml(task.detail)}</textarea>
          <label for="status${index}">任务状态</label>
          <select id="status${index}">
            <option value="completed" ${task.status === "completed" ? "selected" : ""}>🟢 已完成</option>
            <option value="working" ${task.status === "working" ? "selected" : ""}>🟡 工作中</option>
            <option value="attention" ${task.status === "attention" ? "selected" : ""}>🔴 有错误 / 需要决策</option>
          </select>
        </section>`).join("");
    }
    async function load() {
      try {
        const response = await fetch("/tasks", {cache: "no-store"});
        if (response.ok) tasks = await response.json();
      } catch (error) {
        console.warn(error);
      }
      render();
    }
    async function save() {
      const next = [];
      for (let index = 0; index < 3; index += 1) {
        next.push({
          title: document.getElementById("title" + index).value,
          detail: document.getElementById("detail" + index).value,
          status: document.getElementById("status" + index).value
        });
      }
      const response = await fetch("/tasks", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(next)
      });
      const msg = document.getElementById("msg");
      msg.textContent = response.ok ? "保存成功" : "保存失败";
      if (response.ok) tasks = next;
    }
    document.getElementById("save").addEventListener("click", save);
    load();
  </script>
</body>
</html>"""


class Handler(BaseHTTPRequestHandler):
    server_version = "ChatGPTUsageBridge/1.0"

    def _headers(self, content_type: str, length: int) -> None:
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", str(length))

    def send_json(self, code: int, value: Any) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self._headers("application/json; charset=utf-8", len(body))
        self.end_headers()
        self.wfile.write(body)

    def send_html(self, code: int, value: str) -> None:
        body = value.encode("utf-8")
        self.send_response(code)
        self._headers("text/html; charset=utf-8", len(body))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self._headers("text/plain; charset=utf-8", 0)
        self.end_headers()

    def do_GET(self) -> None:
        if self.path == "/usage":
            self.send_json(200, snapshot())
            return
        if self.path == "/tasks":
            self.send_json(200, read_tasks())
            return
        if self.path == "/health":
            self.send_json(200, {"ok": True, "service": "chatgpt-usage-bridge"})
            return
        if self.path in ("/", "/manage"):
            self.send_html(200, MANAGE_HTML)
            return
        self.send_json(404, {"error": "not found"})

    def do_POST(self) -> None:
        if self.path not in ("/usage", "/tasks"):
            self.send_json(404, {"error": "not found"})
            return
        if self.path == "/tasks" and self.client_address[0] not in ("127.0.0.1", "::1"):
            self.send_json(403, {"error": "task edits are local-only"})
            return

        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0
        if size <= 0 or size > MAX_PAYLOAD_BYTES:
            self.send_json(400, {"error": "invalid payload size"})
            return

        try:
            payload = json.loads(self.rfile.read(size))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json(400, {"error": "invalid JSON"})
            return

        if self.path == "/tasks":
            self.send_json(200, {"ok": True, "tasks": write_tasks(payload)})
            return

        try:
            five = int(payload["five_hour_remaining_pct"])
            weekly = int(payload["weekly_remaining_pct"])
            if not (0 <= five <= 100 and 0 <= weekly <= 100):
                raise ValueError
            five_reset = str(payload.get("five_hour_reset", "--"))[:40]
            weekly_reset = str(payload.get("weekly_reset", "--"))[:40]
        except (KeyError, TypeError, ValueError):
            self.send_json(400, {"error": "invalid quota snapshot"})
            return

        with _lock:
            _latest_usage.clear()
            _latest_usage.update(
                {
                    "valid": True,
                    "five_hour_remaining_pct": five,
                    "weekly_remaining_pct": weekly,
                    "five_hour_reset": five_reset,
                    "weekly_reset": weekly_reset,
                    "_received_at": time.time(),
                }
            )
        self.send_json(200, {"ok": True})

    def log_message(self, format: str, *args: Any) -> None:
        # Keep the console useful without logging every ESP32 poll.
        if self.path in ("/usage", "/health") and self.command == "GET":
            return
        super().log_message(format, *args)


class ReusableHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


def main() -> None:
    if not TASKS_FILE.exists():
        write_tasks(DEFAULT_TASKS)
    server = ReusableHTTPServer((HOST, PORT), Handler)
    print(f"ESP32 endpoint: http://PC_IPV4:{PORT}/usage")
    print(f"Task editor:    http://127.0.0.1:{PORT}/manage")
    print("Keep the signed-in ChatGPT Settings > Usage page open in Edge.")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopping bridge")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

