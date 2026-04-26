# PaperS3 Buddy — Claude Code plugin

A Claude Code companion running on a PaperS3 (4.7" e-ink, 540×960,
touch, ESP32-S3). The Paper mirrors every Claude Code session on your
desk: project/branch, model, token budget, recent activity, Claude's
latest reply. Tool activity is mirrored to the display without blocking
execution; Claude's `AskUserQuestion` still lands as four tappable
option buttons.

## What it does

- **Real-time dashboard** — top band shows project + branch (or a
  multi-session list), current Claude model, and the focused session's
  current context-window usage with a budget progress bar.
- **Tool activity view** — PreToolUse hooks display the tool content
  (Bash command, Edit diff, Write path + preview, etc.) so you can see
  what Claude is doing. Ordinary tool calls are auto-allowed.
- **Touch-answer questions** — `AskUserQuestion` renders its options
  as big tappable cards. Tap one → daemon returns the chosen label to
  Claude, which continues using that answer without asking again in
  the terminal.
- **Latest reply panel** — last assistant prose is pulled from the
  session's `transcript_path` and wrapped onto the device.
- **CJK support** — a 3.4MB Chinese/Japanese TTF lives on LittleFS;
  all text (UI labels, Claude replies, user prompts, tool bodies) is
  rendered via FreeType with UTF-8-aware line wrapping.
- **Settings page** — tap the "SETTINGS" label top-right to see
  transport mode, session counts, battery, DND, budget, uptime.
- **ASCII cat buddy** — a 5-line cat in the footer changes expression
  with state (idle / busy / attention / celebrate / DND / sleep).
- **Two transports** — USB serial (default, zero setup) or BLE (Nordic
  UART Service, encrypted pairing with macOS system passkey dialog).
- **Resilient fallback** — if the daemon isn't running, hooks are
  silent no-ops (`curl ... || echo '{}'`), so Claude Code keeps
  working normally.

## Install

Prereqs: [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/),
Homebrew (Apple Silicon), and a USB-C cable to the Paper.

```
/buddy-install
```

The install runs `scripts/install.sh`, which:

1. Verifies PlatformIO is present
2. Installs Python deps (`pyserial`, `bleak` for BLE)
3. Patches the PlatformIO `mklittlefs` x86_64 binary on Apple Silicon
   (`brew install mklittlefs` + symlink) — required for `uploadfs`
4. Merges the hook block into `~/.claude/settings.json` (backs up first)
5. Offers to flash firmware **and** filesystem (CJK font) if a PaperS3
   is on USB
6. Starts the daemon in the background

## Daily use

```
/buddy-start      # start daemon (idempotent)
/buddy-stop       # stop daemon
/buddy-status     # current state: daemon pid, serial device, hooks, recent logs
/buddy-flash      # rebuild + reflash firmware AND filesystem
```

## Transport

Default is `BUDDY_TRANSPORT=auto` — USB serial if a device is plugged in,
else BLE. First BLE connect triggers macOS's system pairing dialog
(6-digit passkey shown on the Paper). Subsequent reconnects are automatic.

Force one or the other with an env var before starting:

```
BUDDY_TRANSPORT=ble    /buddy-start
BUDDY_TRANSPORT=serial /buddy-start
```

## Budget bar

Set `BUDDY_BUDGET=1000000` to show a context-window budget bar on the Paper.
Zero (default) hides the bar. The displayed usage comes from the focused
session's latest assistant usage footprint in `transcript_path`:
`input_tokens + cache_creation_input_tokens + cache_read_input_tokens + output_tokens`.

## Files

```
plugin.json              — manifest (name: m5-paper-buddy)
commands/                — /buddy-* slash commands
scripts/                 — install / install-hooks / start / stop / status / flash / common
settings/hooks.json      — hook block merged into ~/.claude/settings.json
```

State directory: `~/.claude-buddy/` (pid file, daemon log).

## Firmware notes

The PaperS3 firmware lives in the parent repo's `src/paper/`. Key parts:

- `main.cpp` — UI, state machine, touch handling, settings page
- `data_paper.h` — TamaState + line-buffered JSON parser (UTF-8 safe)
- `xfer_paper.h` — status responses, non-ASCII-safe sanitization
- `buddy_frames.h` — ASCII cat art (6 states)

Partition table is `partitions-m5paper.csv` — app 3MB + LittleFS 13MB to
fit the CJK font. The default PlatformIO env is `papers3`; set
`BUDDY_PIO_ENV=m5paper` if you need the legacy M5Paper build.

## Uninstall

```
/buddy-stop
# remove the hook entries you'd like from ~/.claude/settings.json
# (the install backed it up to settings.json.buddy-backup-<timestamp>)
rm -rf ~/.claude-buddy
```
