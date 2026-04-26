#!/usr/bin/env python3
"""Bridge Claude Code ↔ M5Paper buddy.

Stands in for the Claude Desktop app. Hook flow:

  Claude Code hook  ──POST──▶  this daemon  ──serial/BLE──▶  M5Paper
                                     ▲                           │
                                     └───── permission ack ──────┘

Two transports:
  - USB serial: zero-setup, autodetects /dev/cu.usbmodem* (M5Paper CDC)
    or /dev/cu.usbserial-* (older FTDI/CP210x boards).
  - BLE (Nordic UART Service via bleak): wireless. First connect triggers
    macOS's system pairing dialog — enter the 6-digit passkey shown on
    the Paper. After that, the daemon auto-reconnects whenever both sides
    are alive.

Heartbeat extensions vs the stock desktop protocol (firmware ignores
unknown fields, so this stays backward compatible):
  project / branch / dirty   — session's git context
  budget                      — daily token budget bar
  model                       — current Claude model
  assistant_msg               — last prose reply pulled from transcript
  prompt.body                 — full approval content (diff / full command)
  prompt.kind                 — "permission" or "question"
  prompt.options              — AskUserQuestion options (rendered as buttons)

Usage:
    python3 tools/claude_code_bridge.py                    # auto: serial first, else BLE
    python3 tools/claude_code_bridge.py --transport ble    # force BLE
    python3 tools/claude_code_bridge.py --transport serial # force serial
    python3 tools/claude_code_bridge.py --budget 1000000
"""

import argparse
import asyncio
import glob
import heapq
import json
import os
import re
import subprocess
import sys
import threading
import time
from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

# Nordic UART Service UUIDs — match the firmware's ble_bridge.cpp.
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # central → device (write)
NUS_TX_UUID      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # device → central (notify)

# -----------------------------------------------------------------------------
# Shared state
# -----------------------------------------------------------------------------

STATE_LOCK = threading.Lock()

SESSIONS_RUNNING = set()
SESSIONS_TOTAL   = set()
SESSIONS_WAITING = set()
SESSION_META     = {}          # sid -> {cwd, project, branch, dirty, checked_at}
TRANSCRIPT       = deque(maxlen=8)
TOKENS_TOTAL     = 0
TOKENS_TODAY     = 0
ACTIVE_PROMPT    = None        # currently-focused prompt shown on device
PENDING_PROMPTS  = {}          # prompt_id -> prompt dict (all unresolved)
PENDING          = {}          # prompt_id -> {"event", "decision"}

BUDGET_LIMIT        = 0
MODEL_NAME          = ""
ASSISTANT_MSG       = ""                # global fallback when no session is focused
SESSION_ASSISTANT   = {}                # sid -> latest assistant text (per-session)
SESSION_ASSISTANT_AT = {}               # sid -> timestamp of latest assistant text
FOCUSED_SID         = None              # user-picked focused session (for dashboard)
TRANSPORT           = None
BUMP_EVENT          = threading.Event()


def log(*a, **kw):
    print(*a, file=sys.stderr, flush=True, **kw)


def now_hm():
    return datetime.now().strftime("%H:%M")


def add_transcript(line: str):
    line = normalize_display_text(line)[:80]
    if not line:
        return
    with STATE_LOCK:
        if TRANSCRIPT and TRANSCRIPT[0][6:] == line:
            TRANSCRIPT[0] = f"{now_hm()} {line}"
            return
        TRANSCRIPT.appendleft(f"{now_hm()} {line}")


def normalize_display_text(text: str) -> str:
    return " ".join((text or "").split())


def dedupe_consecutive(items):
    out = []
    for item in items:
        if item and (not out or item != out[-1]):
            out.append(item)
    return out


def collapse_repeated_reply(text: str) -> str:
    text = normalize_display_text(text)
    if not text:
        return ""

    # Some transcript variants duplicate the same prose block verbatim.
    words = text.split()
    if len(text) >= 20 and len(words) >= 4 and len(words) % 2 == 0:
        half = len(words) // 2
        if words[:half] == words[half:]:
            text = " ".join(words[:half])

    chunks = re.split(r"(?<=[。！？.!?])\s+", text)
    chunks = dedupe_consecutive([normalize_display_text(chunk) for chunk in chunks])
    return " ".join(chunks)


def format_activity(tool: str, hint: str) -> str:
    hint = normalize_display_text(hint)
    if not hint:
        return tool
    return f"{tool}: {hint[:60]}"


def extract_text_fragments(content) -> list[str]:
    if isinstance(content, str):
        return [content]
    if not isinstance(content, list):
        return []

    parts = []
    for block in content:
        if not isinstance(block, dict) or block.get("type") != "text":
            continue
        text = block.get("text")
        if isinstance(text, str) and text.strip():
            parts.append(text)
    return parts


def read_jsonl_tail(path: str, max_bytes: int = 262144) -> list[str]:
    if not path or not os.path.exists(path):
        return []
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - max_bytes))
            return f.read().decode("utf-8", errors="replace").splitlines()
    except Exception:
        return []


def parse_event_time(obj: dict):
    ts = obj.get("timestamp")
    if not isinstance(ts, str) or not ts:
        return None
    try:
        return datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone()
    except ValueError:
        return None


def assistant_messages_from_record(obj: dict) -> list[dict]:
    messages = []

    data = obj.get("data")
    if isinstance(data, dict):
        progress = data.get("message")
        if isinstance(progress, dict):
            nested = progress.get("message")
            if isinstance(nested, dict) and nested.get("role") == "assistant":
                messages.append(nested)

    msg = obj.get("message")
    if isinstance(msg, dict) and msg.get("role") == "assistant":
        messages.append(msg)

    if obj.get("role") == "assistant":
        messages.append(obj)

    return messages


def context_tokens_from_usage(usage: dict) -> int:
    # Claude Code splits the effective prompt footprint across direct
    # input tokens and cache read/write buckets. The CLI context meter
    # reflects the full prompt plus current output, so mirror that sum.
    total = 0
    for key in (
        "input_tokens",
        "cache_creation_input_tokens",
        "cache_read_input_tokens",
        "output_tokens",
    ):
        try:
            total += int(usage.get(key, 0) or 0)
        except (TypeError, ValueError):
            continue
    return total


def session_recency_key(sid: str) -> float:
    meta = SESSION_META.get(sid) or {}
    return max(
        float(meta.get("checked_at", 0) or 0),
        float(SESSION_ASSISTANT_AT.get(sid, 0) or 0),
    )


def sync_global_assistant_msg() -> None:
    global ASSISTANT_MSG

    best_sid = None
    best_key = (float("-inf"), "")
    for sid, text in SESSION_ASSISTANT.items():
        if not text:
            continue
        key = (float(SESSION_ASSISTANT_AT.get(sid, 0) or 0), sid)
        if key > best_key:
            best_sid = sid
            best_key = key

    latest = SESSION_ASSISTANT.get(best_sid, "") if best_sid else ""
    if latest and latest != ASSISTANT_MSG:
        ASSISTANT_MSG = latest
        BUMP_EVENT.set()


def extract_user_prompt_text(content) -> str:
    if isinstance(content, str):
        text = content.strip()
        if text and not text.startswith("<"):
            return normalize_display_text(text)
        return ""

    if not isinstance(content, list):
        return ""

    for block in content:
        if not isinstance(block, dict) or block.get("type") != "text":
            continue
        text = str(block.get("text", "")).strip()
        if text and not text.startswith("<"):
            return normalize_display_text(text)
    return ""


def extract_tool_activity(msg: dict) -> str:
    content = msg.get("content")
    if not isinstance(content, list):
        return ""

    for block in content:
        if not isinstance(block, dict) or block.get("type") != "tool_use":
            continue
        tool = str(block.get("name", "?"))
        tin = block.get("input") if isinstance(block.get("input"), dict) else {}
        return format_activity(tool, hint_from_tool(tool, tin) or body_from_tool(tool, tin))
    return ""


def extract_recent_activity(path: str, limit: int = 8):
    events = []
    for line in read_jsonl_tail(path):
        line = line.strip()
        if not line or not line.startswith("{"):
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue

        when = parse_event_time(obj)
        msg = obj.get("message", obj)
        text = ""
        if isinstance(msg, dict):
            role = msg.get("role")
            if role == "user":
                prompt = extract_user_prompt_text(msg.get("content"))
                if prompt:
                    text = f"> {prompt[:60]}"
            elif role == "assistant":
                text = extract_tool_activity(msg)

        if when and text:
            events.append((when, text))

    return events[-limit:]


def recent_transcript_paths(limit: int = 6) -> list[Path]:
    root = Path.home() / ".claude" / "projects"
    if not root.exists():
        return []
    try:
        return heapq.nlargest(limit, root.glob("*/*.jsonl"), key=lambda p: p.stat().st_mtime)
    except Exception:
        return []


def bootstrap_recent_state() -> None:
    global ASSISTANT_MSG, MODEL_NAME

    paths = recent_transcript_paths(limit=6)
    if not paths:
        return

    recovered = []
    activity = []

    for path in paths:
        sid = path.stem
        SESSION_TRANSCRIPT_PATH.setdefault(sid, str(path))
        refresh_transcript_state(sid, str(path))

        if sid in SESSION_ASSISTANT:
            recovered.append(sid)

        if not MODEL_NAME:
            model = SESSION_MODEL.get(sid)
            if model:
                MODEL_NAME = model

        activity.extend(extract_recent_activity(str(path), limit=8))

    if not ASSISTANT_MSG:
        for path in paths:
            latest = extract_last_assistant(str(path))
            if latest:
                ASSISTANT_MSG = latest
                break

    if activity:
        activity.sort(key=lambda item: item[0])
        compact = []
        for when, text in activity:
            text = normalize_display_text(text)[:80]
            if not text:
                continue
            if compact and compact[-1][1] == text:
                compact[-1] = (when.strftime("%H:%M"), text)
                continue
            compact.append((when.strftime("%H:%M"), text))
        compact = compact[-TRANSCRIPT.maxlen:]
        with STATE_LOCK:
            TRANSCRIPT.clear()
            for hm, text in compact:
                TRANSCRIPT.appendleft(f"{hm} {text}")

    if ASSISTANT_MSG or TRANSCRIPT:
        log("[bootstrap] replies=%u activity=%u" % (
            len(recovered),
            len(TRANSCRIPT),
        ))


# -----------------------------------------------------------------------------
# Transport abstraction. Device I/O is line-based JSON — transports deliver
# bytes one at a time via an on_byte() callback and accept full frames via
# write(). A line buffer lives in the daemon (below), not in the transport.
# -----------------------------------------------------------------------------

class Transport:
    def start(self, on_byte, on_connect=None): raise NotImplementedError
    def write(self, data: bytes): raise NotImplementedError
    def connected(self) -> bool: raise NotImplementedError


def list_serial_candidates():
    """Best-effort USB serial discovery across macOS/Linux.

    Order matters: prefer macOS cu.* devices for active outbound opens, then
    tty.* aliases, then Linux ACM/USB serial nodes.
    """
    seen = set()
    out = []
    for pattern in (
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial-*",
        "/dev/tty.usbmodem*",
        "/dev/tty.usbserial-*",
        "/dev/ttyACM*",
        "/dev/ttyUSB*",
    ):
        for path in sorted(glob.glob(pattern)):
            if path in seen:
                continue
            seen.add(path)
            out.append(path)
    return out


class SerialTransport(Transport):
    def __init__(self, port=None, allow_scan=True):
        import serial
        self._serial = serial
        self._preferred_port = port
        self._port_name = None
        self._allow_scan = allow_scan
        self.ser = None
        self._io_lock = threading.Lock()
        self._open_lock = threading.Lock()
        self._connected_evt = threading.Event()
        self._on_connect = None
        self._last_open_error = ""
        self._last_no_device_log = 0.0

    def start(self, on_byte, on_connect=None):
        self._on_connect = on_connect
        threading.Thread(target=self._reader, args=(on_byte,), daemon=True).start()

    def _candidate_ports(self):
        ports = []
        for path in (self._port_name, self._preferred_port):
            if path and path not in ports:
                ports.append(path)
        if self._allow_scan:
            for path in list_serial_candidates():
                if path not in ports:
                    ports.append(path)
        return ports

    def _mark_disconnected(self, reason):
        with self._io_lock:
            ser = self.ser
            self.ser = None
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
        if self._connected_evt.is_set():
            port = self._port_name or self._preferred_port or "(unknown)"
            log(f"[serial] lost {port}: {reason}")
        self._connected_evt.clear()

    def _ensure_open(self):
        if self._connected_evt.is_set() and self.ser is not None:
            return True

        with self._open_lock:
            if self._connected_evt.is_set() and self.ser is not None:
                return True

            ports = self._candidate_ports()
            if not ports:
                now = time.time()
                if now - self._last_no_device_log >= 5:
                    log("[serial] no USB serial device found, retrying...")
                    self._last_no_device_log = now
                return False

            last_error = ""
            for port in ports:
                try:
                    # Opening some ESP boards with pyserial's default DTR/RTS
                    # state can reset the MCU immediately. Configure the line
                    # state before open so the USB CDC device stays up.
                    ser = self._serial.Serial(
                        port=None,
                        baudrate=115200,
                        timeout=0.2,
                        write_timeout=1.0,
                        rtscts=False,
                        dsrdtr=False,
                    )
                    ser.dtr = False
                    ser.rts = False
                    ser.port = port
                    ser.open()
                    with self._io_lock:
                        self.ser = ser
                    self._port_name = port
                    self._connected_evt.set()
                    self._last_open_error = ""
                    time.sleep(0.2)   # let the port settle before talking
                    try:
                        ser.reset_input_buffer()
                    except Exception:
                        pass
                    log(f"[serial] opened {port}")
                    if self._on_connect:
                        self._on_connect()
                    return True
                except Exception as e:
                    last_error = f"{port}: {e}"

            if last_error and last_error != self._last_open_error:
                log(f"[serial] open fail: {last_error}")
                self._last_open_error = last_error
            return False

    def _reader(self, on_byte):
        while True:
            if not self._ensure_open():
                time.sleep(1)
                continue
            with self._io_lock:
                ser = self.ser
            try:
                chunk = ser.read(256)
            except Exception as e:
                self._mark_disconnected(f"read failed: {e}")
                time.sleep(1)
                continue
            for b in chunk:
                on_byte(b)

    def write(self, data: bytes):
        if not self._ensure_open():
            return
        with self._io_lock:
            ser = self.ser
        try:
            for off in range(0, len(data), 64):
                ser.write(data[off:off + 64])
                ser.flush()
                if off + 64 < len(data):
                    time.sleep(0.003)
        except Exception as e:
            self._mark_disconnected(f"write failed: {e}")

    def connected(self): return self._connected_evt.is_set()


class BLETransport(Transport):
    """BLE Central via bleak.

    Runs an asyncio event loop on a dedicated thread. Scans for a device
    advertising a name starting with "Claude-", connects, subscribes to
    the Nordic UART TX characteristic for notifications, and exposes a
    thread-safe write() that marshals back onto the asyncio loop.

    Reconnects automatically on disconnect or scan failure.
    """
    def __init__(self, name_prefix="Claude-"):
        self._name_prefix = name_prefix
        self._loop  = None
        self._client = None
        self._thread = None
        self._on_byte = None
        self._on_connect = None
        self._connected_evt = threading.Event()

    def start(self, on_byte, on_connect=None):
        self._on_byte = on_byte
        self._on_connect = on_connect
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        try:
            self._loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self._loop)
            self._loop.run_until_complete(self._main())
        except Exception as e:
            log(f"[ble] thread crashed: {e!r}")

    async def _main(self):
        try:
            from bleak import BleakScanner, BleakClient
        except ImportError:
            log("[ble] bleak not installed. run: pip install bleak")
            return

        while True:
            log(f"[ble] scanning for '{self._name_prefix}*'...")
            device = None
            try:
                device = await BleakScanner.find_device_by_filter(
                    lambda d, ad: bool(d.name) and d.name.startswith(self._name_prefix),
                    timeout=10.0,
                )
            except Exception as e:
                log(f"[ble] scan error: {e}")

            if not device:
                log("[ble] no device found, retrying in 5s")
                await asyncio.sleep(5)
                continue

            log(f"[ble] connecting to {device.name} ({device.address})")
            try:
                # Bleak's context manager handles disconnect on exit. We stay
                # inside it as long as the link is alive.
                async with BleakClient(device) as client:
                    self._client = client

                    def _on_notify(_sender, data: bytearray):
                        for b in data:
                            self._on_byte(b)
                    await client.start_notify(NUS_TX_UUID, _on_notify)

                    self._connected_evt.set()
                    log("[ble] connected")
                    # Fire the connect callback on a SEPARATE thread. Calling
                    # it inline here deadlocks: the callback does sync writes
                    # that marshal back onto this asyncio loop, but the loop
                    # is blocked waiting for the callback to return.
                    if self._on_connect:
                        threading.Thread(
                            target=self._on_connect, daemon=True,
                            name="ble-handshake",
                        ).start()

                    while client.is_connected:
                        await asyncio.sleep(1.0)
                    log("[ble] link lost")
            except Exception as e:
                log(f"[ble] client error: {e!r}")
            finally:
                self._client = None
                self._connected_evt.clear()

            await asyncio.sleep(2)

    def write(self, data: bytes):
        client = self._client
        if client is None or not client.is_connected:
            return
        try:
            fut = asyncio.run_coroutine_threadsafe(
                client.write_gatt_char(NUS_RX_UUID, data, response=False),
                self._loop,
            )
            fut.result(timeout=3)
        except Exception as e:
            log(f"[ble] write fail: {e!r}")

    def connected(self): return self._connected_evt.is_set()


# -----------------------------------------------------------------------------
# Line-based RX parsing — transport delivers bytes, we assemble JSON lines.
# -----------------------------------------------------------------------------

_rx_buf = bytearray()


def on_rx_byte(b: int):
    global _rx_buf
    if b in (0x0A, 0x0D):   # \n or \r
        if _rx_buf:
            raw = bytes(_rx_buf)
            _rx_buf = bytearray()
            try:
                line = raw.decode("utf-8", errors="replace")
            except Exception:
                return
            log(f"[dev<] {line}")
            if not line.startswith("{"):
                return
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                return
            cmd = obj.get("cmd")
            if cmd == "permission":
                pid = obj.get("id")
                h = PENDING.get(pid)
                if h:
                    h["decision"] = obj.get("decision")
                    h["event"].set()
            elif cmd == "focus_session":
                # Device tapped a session row on the dashboard. Switch the
                # FOCUSED_SID so the next heartbeat sends that session's
                # project / branch / latest-reply. Does NOT affect the
                # pending-approval FIFO — approvals keep popping in order.
                global FOCUSED_SID
                FOCUSED_SID = obj.get("sid") or None
                BUMP_EVENT.set()
    else:
        if len(_rx_buf) < 4096:   # sanity cap; devices don't send this much anyway
            _rx_buf.append(b)


def send_line(obj: dict):
    if TRANSPORT is None:
        return

    def _sanitize_device_value(v):
        if isinstance(v, str):
            # PaperS3's current built-in font path handles ASCII + common BMP
            # CJK well, but some emoji / non-BMP symbols can wedge rendering.
            # Strip those for device display only.
            return "".join(ch for ch in v if ord(ch) <= 0xFFFF and (ord(ch) >= 0x20 or ch in "\n\r\t"))
        if isinstance(v, list):
            return [_sanitize_device_value(x) for x in v]
        if isinstance(v, dict):
            return {k: _sanitize_device_value(val) for k, val in v.items()}
        return v

    obj = _sanitize_device_value(obj)
    data = (json.dumps(obj, separators=(",", ":"), ensure_ascii=False) + "\n").encode()
    TRANSPORT.write(data)


def push_heartbeat(reason: str):
    hb = build_heartbeat()
    encoded = json.dumps(hb, separators=(",", ":"), ensure_ascii=False).encode()
    log("[hb] %s total=%u running=%u waiting=%u msg=%r asst=%u" % (
        reason,
        hb.get("total", 0),
        hb.get("running", 0),
        hb.get("waiting", 0),
        hb.get("msg", ""),
        len(hb.get("assistant_msg", "")),
    ))
    log("[hb] bytes=%u" % len(encoded))
    TRANSPORT.write(encoded + b"\n")


# -----------------------------------------------------------------------------
# Git / project introspection — unchanged from the previous revision.
# -----------------------------------------------------------------------------

GIT_TTL_SEC = 10


def _git(cwd, *args, timeout=2.0):
    try:
        out = subprocess.run(("git", *args), cwd=cwd, capture_output=True,
                             text=True, timeout=timeout, check=False)
        return out.stdout.strip() if out.returncode == 0 else ""
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return ""


def refresh_git(sid: str, cwd: str):
    if not cwd or not os.path.isdir(cwd):
        return
    now = time.time()
    meta = SESSION_META.get(sid) or {}
    if meta.get("cwd") == cwd and (now - meta.get("checked_at", 0)) < GIT_TTL_SEC:
        return
    root = _git(cwd, "rev-parse", "--show-toplevel") or cwd
    SESSION_META[sid] = {
        "cwd": cwd,
        "project":    os.path.basename(root.rstrip("/"))[:39] or "",
        "branch":     _git(cwd, "rev-parse", "--abbrev-ref", "HEAD")[:39],
        "dirty":      sum(1 for ln in _git(cwd, "status", "--porcelain").splitlines() if ln.strip()),
        "checked_at": now,
    }


# -----------------------------------------------------------------------------
# Tool → display hint + body
# -----------------------------------------------------------------------------

HINT_FIELDS = {
    "Bash": "command", "Edit": "file_path", "MultiEdit": "file_path",
    "Write": "file_path", "Read": "file_path", "NotebookEdit": "notebook_path",
    "WebFetch": "url", "WebSearch": "query",
    "Glob": "pattern", "Grep": "pattern",
}


def hint_from_tool(tool: str, tin: dict) -> str:
    field = HINT_FIELDS.get(tool)
    if field and isinstance((tin or {}).get(field), str):
        return tin[field]
    for v in (tin or {}).values():
        if isinstance(v, str):
            return v
    return json.dumps(tin or {})[:60]


def body_from_tool(tool: str, tin: dict) -> str:
    tin = tin or {}

    if tool == "AskUserQuestion":
        # Body is just the question text — options are rendered as touch
        # buttons on the device via prompt.options. Don't duplicate here.
        qs = tin.get("questions")
        if isinstance(qs, list) and qs and isinstance(qs[0], dict):
            q = qs[0].get("question") or qs[0].get("header") or ""
        else:
            q = tin.get("question", "")
        return (q or "").strip()[:500]

    if tool == "Bash":
        cmd  = tin.get("command", "")
        desc = tin.get("description", "")
        return (f"{desc}\n\n$ {cmd}" if desc else f"$ {cmd}")[:500]

    if tool in ("Edit", "MultiEdit"):
        path = tin.get("file_path", "")
        oldv = str(tin.get("old_string", ""))[:180]
        newv = str(tin.get("new_string", ""))[:180]
        return f"{path}\n\n--- old\n{oldv}\n\n+++ new\n{newv}"

    if tool == "Write":
        path    = tin.get("file_path", "")
        content = str(tin.get("content", ""))
        head    = content[:320]
        return f"{path}\n\n{head}{('...' if len(content) > 320 else '')}"

    if tool == "Read":
        return tin.get("file_path", "")

    if tool == "WebFetch":
        url = tin.get("url", "")
        prompt = str(tin.get("prompt", ""))[:200]
        return f"{url}\n\n{prompt}" if prompt else url

    if tool == "WebSearch":
        return str(tin.get("query", ""))[:300]

    if tool in ("Glob", "Grep"):
        parts = [f"pattern: {tin.get('pattern', '')}"]
        if tin.get("path"): parts.append(f"path: {tin['path']}")
        if tin.get("type"): parts.append(f"type: {tin['type']}")
        return "\n".join(parts)[:300]

    try:
        return json.dumps(tin, indent=2)[:500]
    except Exception:
        return str(tin)[:500]


# -----------------------------------------------------------------------------
# Heartbeat construction
# -----------------------------------------------------------------------------

def build_heartbeat() -> dict:
    with STATE_LOCK:
        msg = (f"approve: {ACTIVE_PROMPT['tool']}" if ACTIVE_PROMPT
               else (TRANSCRIPT[0][6:] if TRANSCRIPT else "idle"))
        # tokens_today is now "focused session's current context" — gets
        # filled in below once we resolve which session is focused. Start
        # with zero; a session without transcript data will stay at zero.
        hb = {
            "total":        len(SESSIONS_TOTAL),
            "running":      len(SESSIONS_RUNNING),
            "waiting":      len(SESSIONS_WAITING),
            "msg":          msg[:23],
            "entries":      list(TRANSCRIPT),
            "tokens":       0,
            "tokens_today": 0,
        }
        if ACTIVE_PROMPT:
            p = {
                "id":   ACTIVE_PROMPT["id"],
                "tool": ACTIVE_PROMPT["tool"][:19],
                "hint": ACTIVE_PROMPT["hint"][:43],
                "body": ACTIVE_PROMPT["body"][:500],
                "kind": ACTIVE_PROMPT.get("kind", "permission"),
            }
            opts = ACTIVE_PROMPT.get("option_labels") or []
            if opts: p["options"] = opts[:4]
            # Identify which session this prompt is from — so the user
            # can see on the Paper which project/window needs an answer.
            sid = ACTIVE_PROMPT.get("session_id", "")
            if sid:
                p["sid"] = sid[:8]
                meta = SESSION_META.get(sid) or {}
                p["project"] = meta.get("project", "")[:23]
            hb["prompt"] = p

        # Waiting count (for the "N waiting" indicator); approval cards
        # FIFO out of this queue so we don't need to ship the full list.
        # (Earlier revisions sent a `pending[]` tab strip — removed, user
        # preferred dashboard-level session switching over approval tabs.)

        # sessions array: one compact entry per running session. `focused`
        # marks which one the dashboard should render as primary. Tapping
        # a row sends {"cmd":"focus_session","sid":...} back.
        sessions_list = []
        for sid in list(SESSIONS_TOTAL)[:5]:
            meta = SESSION_META.get(sid) or {}
            sessions_list.append({
                "sid":     sid[:8],
                "full":    sid,
                "proj":    (meta.get("project", "") or "")[:22],
                "branch":  (meta.get("branch", "") or "")[:16],
                "dirty":   meta.get("dirty", 0),
                "running": sid in SESSIONS_RUNNING,
                "waiting": sid in SESSIONS_WAITING,
                "focused": sid == FOCUSED_SID,
            })
        if sessions_list:
            hb["sessions"] = sessions_list
        if BUDGET_LIMIT > 0:   hb["budget"] = BUDGET_LIMIT

        # Resolve which session "focuses" the dashboard view. Priority:
        # 1. User tap (FOCUSED_SID) if still valid
        # 2. Session that raised the current approval
        # 3. Most recently-active running session
        # 4. Most recently-seen session metadata
        # 5. Global latest assistant reply fallback
        sid = None
        if FOCUSED_SID and FOCUSED_SID in SESSION_META:
            sid = FOCUSED_SID
        elif ACTIVE_PROMPT and ACTIVE_PROMPT.get("session_id"):
            sid = ACTIVE_PROMPT["session_id"]
        elif SESSIONS_RUNNING:
            sid = max(SESSIONS_RUNNING, key=session_recency_key)
        elif SESSION_META:
            sid = max(SESSION_META, key=session_recency_key)
        elif SESSIONS_TOTAL:
            sid = max(SESSIONS_TOTAL, key=session_recency_key)

        if sid and sid in SESSION_META:
            m = SESSION_META[sid]
            hb["project"] = m.get("project", "")
            hb["branch"]  = m.get("branch", "")
            hb["dirty"]   = m.get("dirty", 0)

        # Per-session current-turn context usage → the number the budget
        # bar on the device should compare against the model's window.
        if sid:
            ctx = SESSION_CONTEXT.get(sid, 0)
            hb["tokens"] = ctx
            hb["tokens_today"] = ctx

        # Model from the focused session's transcript. Fall back to the
        # legacy global (rarely populated since hook payloads don't carry
        # a `model` field).
        s_model = SESSION_MODEL.get(sid) if sid else None
        if s_model:       hb["model"] = s_model
        elif MODEL_NAME:   hb["model"] = MODEL_NAME

        a_msg = SESSION_ASSISTANT.get(sid) if sid else None
        if a_msg:   hb["assistant_msg"] = a_msg
        elif ASSISTANT_MSG: hb["assistant_msg"] = ASSISTANT_MSG
    return hb


def heartbeat_loop():
    """Send a heartbeat on BUMP (state change) or every 10s if idle.

    Rate-limited to one send per MIN_INTERVAL seconds so a busy second
    window firing hooks constantly doesn't flood the device — the ESP32
    would get stuck trying to parse + redraw every delta and eventually
    hang the watchdog. Bumps during the quiet window are coalesced into
    the next send (the clear-then-wait pattern picks up any new set).
    """
    MIN_INTERVAL = 1.0
    IDLE_INTERVAL = 2.0
    last_sent = 0.0
    while True:
        BUMP_EVENT.wait(timeout=IDLE_INTERVAL)
        BUMP_EVENT.clear()
        # Transcript writes can land slightly after the corresponding hook.
        # Refresh cached per-session transcript-derived fields on each loop
        # so latest reply/model/context eventually converge even when the
        # hook raced ahead of the JSONL append.
        for sid, tp in list(SESSION_TRANSCRIPT_PATH.items()):
            refresh_transcript_state(sid, tp)
        now = time.time()
        since = now - last_sent
        if since < MIN_INTERVAL:
            time.sleep(MIN_INTERVAL - since)
        push_heartbeat("tick")
        last_sent = time.time()


# -----------------------------------------------------------------------------
# Model + transcript helpers (unchanged)
# -----------------------------------------------------------------------------

def short_model(full: str) -> str:
    if not full: return ""
    import re
    s = full.lower()
    family = "Claude"
    for tag, label in (("opus", "Opus"), ("sonnet", "Sonnet"), ("haiku", "Haiku")):
        if tag in s:
            family = label; break
    m = re.search(r"(\d+)[\.\-](\d+)", s)
    if m: return f"{family} {m.group(1)}.{m.group(2)}"
    return family if family != "Claude" else full[:28]


def extract_session_context(path: str) -> int:
    """Return the session's CURRENT context-window usage, approximated
    as the latest assistant usage footprint visible in the transcript.
    Claude Code records cached prompt tokens separately, and progress
    events can arrive before the final top-level assistant append, so
    we sum the cached/direct buckets from the newest assistant record.
    """
    if not path or not os.path.exists(path):
        return 0
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - 131072))
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line or not line.startswith("{"): continue
            try: obj = json.loads(line)
            except json.JSONDecodeError: continue
            for msg in assistant_messages_from_record(obj):
                usage = msg.get("usage")
                if isinstance(usage, dict):
                    return context_tokens_from_usage(usage)
    except Exception:
        pass
    return 0


# Per-session context-window usage (updated on each hook).
SESSION_CONTEXT: dict = {}


def extract_session_model(path: str) -> str:
    """Find the most recent assistant message in the transcript and
    return its `model` field. Hook payloads don't carry model info;
    transcripts do (per assistant turn)."""
    if not path or not os.path.exists(path):
        return ""
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - 131072))
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line or not line.startswith("{"): continue
            try: obj = json.loads(line)
            except json.JSONDecodeError: continue
            for msg in assistant_messages_from_record(obj):
                m = msg.get("model")
                if isinstance(m, str) and m:
                    return m
    except Exception:
        pass
    return ""


SESSION_MODEL: dict = {}
SESSION_TRANSCRIPT_PATH: dict = {}


def resolve_transcript_path(sid: str, cwd: str = "") -> str:
    if not sid:
        return ""
    cached = SESSION_TRANSCRIPT_PATH.get(sid)
    if cached and os.path.exists(cached):
        return cached

    roots = []
    projects_root = Path.home() / ".claude" / "projects"
    if cwd:
        slug = cwd.replace(os.sep, "-")
        if not slug.startswith("-"):
            slug = "-" + slug
        roots.append(projects_root / slug)
    roots.append(projects_root)

    seen = set()
    for root in roots:
        root = Path(root)
        if root in seen or not root.exists():
            continue
        seen.add(root)
        direct = root / f"{sid}.jsonl"
        if direct.exists():
            SESSION_TRANSCRIPT_PATH[sid] = str(direct)
            return str(direct)
        for path in root.rglob(f"{sid}.jsonl"):
            SESSION_TRANSCRIPT_PATH[sid] = str(path)
            return str(path)
    return ""


def refresh_transcript_state(sid: str, tp: str) -> None:
    if not sid or not tp or not os.path.exists(tp):
        return

    m = extract_session_model(tp)
    if m:
        sm = short_model(m)
        if SESSION_MODEL.get(sid) != sm:
            SESSION_MODEL[sid] = sm
            BUMP_EVENT.set()

    latest, latest_at = extract_last_assistant_entry(tp)
    if latest:
        if SESSION_ASSISTANT.get(sid) != latest:
            SESSION_ASSISTANT[sid] = latest
            BUMP_EVENT.set()
        SESSION_ASSISTANT_AT[sid] = latest_at
        sync_global_assistant_msg()

    ctx = extract_session_context(tp)
    if SESSION_CONTEXT.get(sid) != ctx:
        SESSION_CONTEXT[sid] = ctx
        BUMP_EVENT.set()


def extract_last_assistant_entry(path: str) -> tuple[str, float]:
    if not path or not os.path.exists(path):
        return "", 0.0
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - 131072))
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try: obj = json.loads(line)
            except json.JSONDecodeError: continue
            when = parse_event_time(obj)
            msg = obj.get("message", obj)
            if not isinstance(msg, dict): continue
            if msg.get("role") != "assistant": continue
            parts = [
                normalize_display_text(text)
                for text in extract_text_fragments(msg.get("content"))
            ]
            parts = dedupe_consecutive(parts)
            text = collapse_repeated_reply(" ".join(parts))
            if text:
                ts = when.timestamp() if when else os.path.getmtime(path)
                return text[:220], ts
    except Exception as e:
        log(f"[transcript] error: {e}")
    return "", 0.0


def extract_last_assistant(path: str) -> str:
    text, _ = extract_last_assistant_entry(path)
    return text


# -----------------------------------------------------------------------------
# HTTP handler — unchanged in terms of semantics.
# -----------------------------------------------------------------------------

class HookHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args): pass

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length") or "0")
            body = self.rfile.read(n) if n > 0 else b""
            payload = json.loads(body.decode("utf-8")) if body else {}
        except Exception as e:
            return self._reply(400, {"error": str(e)})

        event = payload.get("hook_event_name", "")
        log(f"[hook] {event} session={payload.get('session_id', '')[:8]}")

        sid = payload.get("session_id", "")
        cwd = payload.get("cwd", "")
        if sid:
            with STATE_LOCK:
                SESSIONS_TOTAL.add(sid)
                if event == "Stop":
                    SESSIONS_RUNNING.discard(sid)
                else:
                    SESSIONS_RUNNING.add(sid)
        if sid and cwd:
            refresh_git(sid, cwd)

        global MODEL_NAME, ASSISTANT_MSG
        for k in ("model", "model_id", "assistant_model"):
            v = payload.get(k)
            if isinstance(v, str) and v:
                MODEL_NAME = short_model(v); break

        tp = payload.get("transcript_path")
        if (not isinstance(tp, str) or not tp) and sid:
            tp = resolve_transcript_path(sid, cwd)
        if isinstance(tp, str) and tp and sid:
            if SESSION_TRANSCRIPT_PATH.get(sid) != tp:
                SESSION_TRANSCRIPT_PATH[sid] = tp
                log(f"[transcript] sid={sid[:8]} path={tp}")
            refresh_transcript_state(sid, tp)

        try:
            if   event == "SessionStart":      resp = self._session_start(payload)
            elif event == "Stop":              resp = self._session_stop(payload)
            elif event == "UserPromptSubmit":  resp = self._user_prompt(payload)
            elif event == "PreToolUse":        resp = self._pretool(payload)
            elif event == "PostToolUse":       resp = self._posttool(payload)
            else:                              resp = {}
        except Exception as e:
            log(f"[hook] handler error: {e!r}"); resp = {}

        self._reply(200, resp)

    def _reply(self, code: int, obj: dict):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try: self.wfile.write(body)
        except BrokenPipeError: pass

    def _session_start(self, p):
        sid = p.get("session_id", "")
        with STATE_LOCK:
            SESSIONS_TOTAL.add(sid); SESSIONS_RUNNING.add(sid)
        proj = (SESSION_META.get(sid) or {}).get("project", "")
        add_transcript(f"session: {proj}" if proj else "session started")
        BUMP_EVENT.set()
        return {}

    def _session_stop(self, p):
        sid = p.get("session_id", "")
        with STATE_LOCK:
            SESSIONS_RUNNING.discard(sid)
        add_transcript("session done"); BUMP_EVENT.set()
        return {}

    def _user_prompt(self, p):
        prompt = (p.get("prompt") or "").strip().replace("\n", " ")
        if prompt:
            add_transcript(f"> {prompt[:60]}"); BUMP_EVENT.set()
        return {}

    def _posttool(self, p):
        # Ordinary tool activity is already mirrored at PreToolUse with
        # enough detail for the Paper UI. Don't append a second low-signal
        # "... done" line for every tool execution.
        return {}

    def _pretool(self, p):
        sid  = p.get("session_id", "")
        tool = p.get("tool_name", "?")
        tin  = p.get("tool_input") or {}
        hint = hint_from_tool(tool, tin)
        body = body_from_tool(tool, tin)

        if tool != "AskUserQuestion":
            # Display-only mode for tool execution: the Paper mirrors the
            # action Claude is about to take, but never blocks execution
            # waiting for device-side confirmation.
            add_transcript(format_activity(tool, hint or body))
            BUMP_EVENT.set()

            if p.get("permission_mode") == "bypassPermissions":
                return {"hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "allow",
                    "permissionDecisionReason": "bypass-permissions mode",
                }}

            return {"hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "allow",
                "permissionDecisionReason": "Displayed on M5Paper; no device confirmation required",
            }}

        kind = "question"
        option_labels = []
        qs = tin.get("questions")
        if isinstance(qs, list) and qs and isinstance(qs[0], dict):
            for o in (qs[0].get("options") or [])[:4]:
                option_labels.append(str(o.get("label")) if isinstance(o, dict) else str(o))
        else:
            for o in (tin.get("options") or [])[:4]:
                option_labels.append(str(o.get("label")) if isinstance(o, dict) else str(o))

        # Display-only mode: the Paper mirrors the action Claude is about
        # to take for ordinary tools. Question prompts still go through the
        # existing device-side selection flow.
        add_transcript(format_activity(tool, hint or body))
        prompt_id = f"req_{int(time.time() * 1000)}_{os.getpid()}"
        event = threading.Event()
        holder = {"event": event, "decision": None}
        PENDING[prompt_id] = holder

        prompt_obj = {
            "id": prompt_id, "tool": tool, "hint": hint, "body": body,
            "kind": kind, "option_labels": option_labels, "session_id": sid,
        }

        global ACTIVE_PROMPT
        with STATE_LOCK:
            SESSIONS_WAITING.add(sid)
            PENDING_PROMPTS[prompt_id] = prompt_obj
            if ACTIVE_PROMPT is None:
                ACTIVE_PROMPT = prompt_obj
        BUMP_EVENT.set()

        try:
            got = event.wait(timeout=30)
            decision = holder["decision"] if got else None
            if isinstance(decision, str) and decision.startswith("option:"):
                time.sleep(0.6)
        finally:
            PENDING.pop(prompt_id, None)
            with STATE_LOCK:
                SESSIONS_WAITING.discard(sid)
                PENDING_PROMPTS.pop(prompt_id, None)
                if ACTIVE_PROMPT and ACTIVE_PROMPT["id"] == prompt_id:
                    ACTIVE_PROMPT = next(iter(PENDING_PROMPTS.values()), None)
            BUMP_EVENT.set()

        if isinstance(decision, str) and decision.startswith("option:"):
            try: idx = int(decision.split(":", 1)[1])
            except ValueError: idx = -1
            label = option_labels[idx] if 0 <= idx < len(option_labels) else ""
            add_transcript(f"{tool} → {label[:30]}"); BUMP_EVENT.set()
            return {"hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": (
                    f"The user answered on the M5Paper buddy device: "
                    f"'{label}' (option {idx + 1}). Proceed using this answer "
                    f"directly — do NOT call AskUserQuestion again."
                ),
            }}

        if decision == "deny":
            add_transcript(f"{tool} deny"); BUMP_EVENT.set()
            return {"hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": (
                    "The user cancelled this question on the M5Paper buddy "
                    "without answering. Ask them directly in the terminal instead."
                ),
            }}

        add_transcript(f"{tool} timeout"); BUMP_EVENT.set()
        return {}


# -----------------------------------------------------------------------------

def tz_offset_seconds() -> int:
    now = time.time()
    local = datetime.fromtimestamp(now)
    utc_dt = datetime(*datetime.fromtimestamp(now, tz=None).utctimetuple()[:6])
    return int((local - utc_dt).total_seconds())


def pick_transport(kind: str) -> Transport:
    """Resolve --transport flag to a concrete Transport. 'auto' tries
    serial first (zero-setup, no BLE permission dance) and falls back
    to BLE if no USB device is found."""
    candidates = list_serial_candidates()

    if kind == "serial":
        if not candidates:
            sys.exit("--transport serial requested but no USB serial device found "
                     "(looked for cu.usbmodem*, cu.usbserial-*, tty.usbmodem*, "
                     "tty.usbserial-*, ttyACM*, ttyUSB*)")
        return SerialTransport(candidates[0], allow_scan=True)

    if kind == "ble":
        return BLETransport()

    # auto
    if candidates:
        log("[transport] serial device found, using USB")
        return SerialTransport(candidates[0], allow_scan=True)
    log("[transport] no serial device, falling back to BLE")
    return BLETransport()


def main():
    global BUDGET_LIMIT, TRANSPORT

    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="explicit serial port (implies --transport serial)")
    ap.add_argument("--transport", choices=("auto", "serial", "ble"), default="auto")
    ap.add_argument("--http-port", type=int, default=9876)
    ap.add_argument("--owner", default=os.environ.get("USER", ""))
    ap.add_argument("--budget", type=int, default=200000,
                    help="context-window limit for the budget bar (default 200K = "
                         "Claude 4.6 standard context; set 1000000 for 1M-context "
                         "beta; set 0 to hide the bar)")
    args = ap.parse_args()

    BUDGET_LIMIT = max(0, args.budget)

    if args.port:
        TRANSPORT = SerialTransport(args.port, allow_scan=False)
    else:
        TRANSPORT = pick_transport(args.transport)

    bootstrap_recent_state()

    # Send the owner + time-sync handshake whenever we (re)connect. For
    # serial, the transport fires on_connect immediately. For BLE, it
    # fires after subscribing to TX notify so the device is ready.
    def _handshake():
        if args.owner:
            send_line({"cmd": "owner", "name": args.owner})
        send_line({"time": [int(time.time()), tz_offset_seconds()]})
        push_heartbeat("handshake")

    TRANSPORT.start(on_rx_byte, on_connect=_handshake)
    threading.Thread(target=heartbeat_loop, daemon=True).start()

    srv = HTTPServer(("127.0.0.1", args.http_port), HookHandler)
    log(f"[http] listening on 127.0.0.1:{args.http_port}  budget={BUDGET_LIMIT}")
    log("[ready] start a Claude Code session with the hooks installed")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        log("\n[exit] bye")


if __name__ == "__main__":
    main()
