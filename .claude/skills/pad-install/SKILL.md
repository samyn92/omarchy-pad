---
name: pad-install
description: Install an Omarchy Pad project (floatkey, stage, lock, or all) on the tablet and restart what changed. Use when the user says to install, ship to the tablet, or try the installed build. Needs sudo, which the user types themselves.
---

# Installing on the tablet

Only install when the user asked for it in this conversation. Every install
needs a fresh yes.

## 1. Check first

- Run the project's tests (`<project>/bin/test`) and make sure they pass.
- Check that the version was bumped if behaviour changed (see the project's
  CLAUDE.md).

## 2. Open a terminal tab for the user

`makepkg -fsi` asks for a sudo password, which the user types. Never pipe or
pass a password.

Open a Terminal panel tab (the `mcp__terminal__run_in_terminal` tool) in the
repo root and run one of these:

| What | Command |
|---|---|
| floatkey | `floatkey/bin/install` |
| stage | `stage/bin/install` |
| both | `bin/install` |
| lock plugin (no sudo; switches the real shell to it) | `lock/bin/install` |

Link the tab, tell the user it is waiting for their password, then end the
turn. Don't wait or poll: the user says when it's done.

## 3. Restart what changed

Once the user says it finished, check the tab for errors
(`mcp__terminal__read_terminal`). Then restart:

- **floatkey:**
  ```sh
  floatkey quit; for i in $(seq 20); do pgrep -x floatkey >/dev/null || break; sleep 0.2; done
  QT_IM_MODULE=fcitx setsid -f /usr/bin/floatkey --hidden >/dev/null 2>&1 </dev/null
  sleep 1; floatkey --version; floatkey status
  ```
  If `fcitx5/` changed, also run
  `systemctl --user restart omarchy-fcitx5.service`.
- **stage:**
  ```sh
  stage quit; sleep 0.5; QT_IM_MODULE=fcitx setsid -f /usr/bin/stage >/dev/null 2>&1 </dev/null
  sleep 1.5; stage --version; stage status
  ```
- **lock:** nothing to restart. It takes effect the next time the screen
  locks. Don't lock to check it.

Why `QT_IM_MODULE=fcitx`: this project's Claude settings set
`QT_IM_MODULE=none` for the tests. A desktop process started from here would
inherit it, and so would every app stage launches. That means no input method
and no dictation in those apps.

Report the version now running, and say what the user should try.
