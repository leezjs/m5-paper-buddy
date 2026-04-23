---
description: Show Claude Buddy daemon + device status at a glance.
---

Prints whether the daemon is running, which serial port (if any) is
present, whether hooks are installed, and the last few daemon log lines.

!`bash -c 'R="${CLAUDE_PLUGIN_ROOT:-$(echo "$PATH" | tr : "\n" | grep -m1 "/m5-paper-buddy/bin$" | sed "s|/bin$||")}"; exec bash "$R/scripts/status.sh"'`
