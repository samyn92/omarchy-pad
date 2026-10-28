---
name: pad-try
description: Run a fresh build of floatkey or stage in place on the tablet (no install), with logging, then go back to the installed one; or try Hyprland config and lock changes in a nested Hyprland. Use when checking a change in the real desktop before installing.
---

# Trying a build without installing

`$SCRATCH` below is your session scratchpad directory.

## floatkey or stage, in place

The live session runs one copy of each. Replace it with the build, and log to
the scratchpad:

```sh
cd <repo>/floatkey && bin/build 2>&1 | grep -E 'error|Built'
floatkey quit; for i in $(seq 20); do pgrep -x floatkey >/dev/null || break; sleep 0.2; done
QT_IM_MODULE=fcitx QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES='floatkey.focus.debug=true' \
  setsid -f ./build/floatkey --hidden >"$SCRATCH/floatkey.log" 2>&1 </dev/null
sleep 1; ./build/floatkey status
```

For stage, use `stage quit` and `./build/stage` with `QT_LOGGING_RULES='stage.debug=true'`.
Keep `QT_IM_MODULE=fcitx`: this project's settings set it to `none` for the
tests, and the apps stage launches would inherit that.

- Ask the user to try the change, then read the log once. Don't keep polling.
- When done, go back to the installed one. It's the same command with
  `/usr/bin/floatkey --hidden` (or `/usr/bin/stage`) and no logging. Do this
  even if the user didn't ask: an in-place build left running looks like the
  installed version.

## Hyprland config, layer rules, the lock: nested

Never change the live config (a hook blocks `hyprctl keyword/repl/eval`).

- **Lock:** `lock/tests/try` (password `test`), and `lock/tests/plug` for a
  keyboard coming and going.
- **Config experiments:** start a nested Hyprland in a window with its own
  config, as `lock/tests/try` does, and drive it with `hyprctl -i <instance>`
  or with its own `HYPRLAND_INSTANCE_SIGNATURE`. The hook lets those through.
  Instance 0 is the live session.

## Screenshots

`grim "$SCRATCH/shot.png"` captures the real screen. Read the image to check
it.
