# lock

A prototype: Omarchy's lock plugin with floatkey's keyboard. `bin/install`
builds it from Omarchy's own lock plugin (`/usr/share/omarchy/shell/plugins/lock`).
Omarchy's `LockView.qml` becomes `UpstreamLockView.qml`, and floatkey's
`KeyIcon.qml` and `layouts.js` are copied in.

- **Try it only with `tests/try`.** It opens a nested Hyprland with a harness
  shell and a throwaway PAM stack (password `test`). `tests/try --real-input`
  uses the real keyboards, and `tests/plug WORK_DIR [--unplug]` fakes one
  coming and going.
- Never lock the real session, and never run a full omarchy-shell copy nested.
  Omarchy's `Service.qml` blanks the real panel on its timer, and the real PAM
  stack's faillock counts wrong passwords against the user's account.
- `bin/install` with no arguments switches the user's real shell to this
  plugin. That counts as an install: ask first.
- After an Omarchy update, the hook from `bin/install-hook` rebuilds it.
