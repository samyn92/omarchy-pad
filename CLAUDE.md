# Omarchy Pad

Omarchy on a tablet, made to feel like an iPad. The goal is to ship it as a
product: an "OS for the Pad" layered on Omarchy. Development happens on the
tablet itself, a OnePlus Pad 4 (aarch64, Adreno 840), which is also the user's
only machine in use: anything that freezes the compositor leaves them stuck,
often with no keyboard attached.

## What belongs here

| Folder | What | Ships as |
|---|---|---|
| `floatkey/` | On-screen keyboard (Qt Quick, layer-shell) plus its fcitx5 addon | `floatkey` package |
| `stage/` | Touch gestures: edge swipes, app launcher, scratchpad console, close and workspace swipes | `stage` package |
| `lock/` | Omarchy's lock screen with floatkey's keyboard (prototype) | Omarchy plugin, `lock/bin/install` |
| `firstboot/` | A touch keyboard (buffyboard) under Omarchy's first-boot owner setup | `omarchy-pad-firstboot` and `buffybox` packages, for the install image |
| `tablet/` | The tablet session: auto-rotation, starting floatkey and stage | `install-tablet-setup.sh` |

Omarchy Pad is meant for any touch tablet. Work specific to the OnePlus Pad
hardware (audio DSP and PipeWire profiles, charging, kernel, firmware) is the
device layer, upstream `iceland-device` / `iceland-mainline`, and does not go
in this repo.

## Commands

Each project has `bin/build`, `bin/test` and `bin/install` (lock has
`bin/install` and `tests/try`). From the root, `bin/test` and `bin/install`
run floatkey and stage. Builds go to `<project>/build/` (ignored).

- `bin/test` runs offscreen with `QT_IM_MODULE=none`. Keep that for any Qt
  binary you run by hand: with fcitx5's module loaded, a test that focuses a
  text field takes fcitx5's input focus from the desktop, and Superwhisper
  dictation then types into the test.
- `bin/install` runs `makepkg -fsi` and needs sudo. Use the `pad-install`
  skill.
- A fresh build can be tried in place without installing: see the `pad-try`
  skill.
- `tests/clipboard` checks copy and paste end to end, after building floatkey
  and stage: a nested Hyprland with no window, a Chromium field and foot,
  driven by `stage simulate pinch` and floatkey's commands. Run it after
  touching floatkey's edit keys or clipboard, or stage's pinch. Real fingers
  can't be simulated there, so what an app does with the touches themselves
  (a tap that drops the selection) still needs a real try.

## Working rules

- **Ask before installing and before committing.** The user types sudo
  passwords themselves: open a terminal tab running the install, wait for them
  to say it's done, then restart what changed.
- **Don't change the live Hyprland config.** `hyprctl keyword`, `hyprctl repl`
  and `hyprctl eval` against the running session once froze the compositor
  (2026-10-27) and needed a reboot. A hook in `.claude/settings.json` blocks
  them. Read-only `hyprctl -j …` queries and `hyprctl dispatch` are fine. For
  config experiments, use a nested Hyprland, as `lock/tests/try` does.
- **Never lock the real session to test the lock.** Use `lock/tests/try`
  (nested Hyprland, throwaway PAM, password `test`). Omarchy's real lock
  `Service.qml` blanks the real panel even from a nested session, and the real
  PAM stack has faillock (deny=10), so wrong test passwords count against the
  real account.
- Don't poll or wait long. Start long-running things in the background and
  check back once.
- Commit messages: a `project: what changed` subject in plain words
  (`floatkey: …`, `stage: …`, `tests: …`), a body that says why. When behaviour
  changes for the user, bump the version where the project's CLAUDE.md says it
  lives.

## Style

C++17 with Qt 6 and QML, qmake. Match the surrounding code: comments explain
*why* in full sentences, the tests are named as behaviours
(`tappingAFieldBringsItUp`), and the READMEs are written for users. Update the
project's README when behaviour or settings change.
