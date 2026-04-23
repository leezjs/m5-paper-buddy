---
description: Stop the Claude Buddy bridge daemon.
---

!`bash -c 'R="${CLAUDE_PLUGIN_ROOT:-$(echo "$PATH" | tr : "\n" | grep -m1 "/m5-paper-buddy/bin$" | sed "s|/bin$||")}"; exec bash "$R/scripts/stop.sh"'`
