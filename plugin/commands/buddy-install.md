---
description: First-time setup for the PaperS3 Buddy — checks PlatformIO, installs Python deps, patches mklittlefs, merges hooks, offers to flash firmware + font, starts the daemon.
---

Run the full install. Safe to re-run; every step is idempotent.

On Apple Silicon the install also patches PlatformIO's x86_64
`mklittlefs` binary (needed to upload the CJK font to LittleFS) by
`brew install mklittlefs` + symlink.

Default flashing target is `papers3`. Override with `BUDDY_PIO_ENV` if you
need the legacy M5Paper targets.

!`bash -c 'R="${CLAUDE_PLUGIN_ROOT:-$(echo "$PATH" | tr : "\n" | grep -m1 "/m5-paper-buddy/bin$" | sed "s|/bin$||")}"; exec bash "$R/scripts/install.sh"'`
