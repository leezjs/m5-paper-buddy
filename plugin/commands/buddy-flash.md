---
description: Build + flash PaperS3 firmware AND filesystem (CJK font). Stops + restarts the daemon around the flash.
---

Runs `pio run -t uploadfs` (filesystem, ~90s) then `pio run -t upload`
(firmware, ~30s). Both are needed: the firmware can't render non-ASCII
without the font on LittleFS.

Default PlatformIO env is `papers3`. Override with `BUDDY_PIO_ENV` if you
need the legacy M5Paper targets.

!`bash -c 'R="${CLAUDE_PLUGIN_ROOT:-$(echo "$PATH" | tr : "\n" | grep -m1 "/m5-paper-buddy/bin$" | sed "s|/bin$||")}"; bash "$R/scripts/stop.sh" || true; bash "$R/scripts/flash.sh" && bash "$R/scripts/start.sh"'`
