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

Logging in by touch, in the `omarchy-pad-lock` package (`pkgbuild/`):
- `omarchy-pad-login.service` writes `/etc/sddm.conf.d/20-omarchy-pad-autologin.conf`
  once, for the owner the first-boot setup made: SDDM logs them in by itself
  (Relogin, so a logout comes back to the lock screen too) into
- `omarchy-pad.desktop`: Omarchy's uwsm session with Hyprland's
  `--locked-cmd /usr/bin/omarchy-pad-lock-at-start`. Hyprland starts locked
  and draws nothing until a lock screen takes the lock over.
- `bin/omarchy-pad-lock-at-start` asks Omarchy's shell to lock until it is
  secure and logs to the journal under its name. Omarchy's shell also takes
  over a lock it finds with no locker by itself, so giving up (600 s) unlocks
  nothing: a slow start still ends on the lock screen. If the shell never
  comes, the screen stays blank, the price of never showing an unlocked
  desktop.
- Test it only nested: `tests/try --start-locked bin/omarchy-pad-lock-at-start`
  (the script talks to the harness there, never to the real shell).
