# floatkey

The on-screen keyboard: a Qt Quick layer-shell overlay, plus a fcitx5 addon
(`fcitx5/`) that tells it when a text field takes or loses focus.

- **Version:** kept in three places: `pkgver` in `pkgbuild/PKGBUILD`, the `--version` line and
  `setApplicationVersion` in `src/main.cpp`. Bump all three together.
- **Running a build in place:** `build/floatkey quit`, wait for the process to
  go, then `setsid -f build/floatkey --hidden >/dev/null 2>&1 </dev/null`. Use
  `/usr/bin/floatkey` to go back to the installed one. `floatkey status` and
  `floatkey show|hide|float|dock|orb` drive it.
- **Logs** go to the user journal (`journalctl --user -t floatkey`), not
  stdout, unless you set `QT_FORCE_STDERR_LOGGING=1`. Focus decisions log with
  `QT_LOGGING_RULES=floatkey.focus.debug=true`.
- **The fcitx5 addon** loads only when fcitx5 starts. After installing a
  changed addon, run `systemctl --user restart omarchy-fcitx5.service`.
- **Settings** live in `~/.config/floatkey/floatkey.conf` and are read at
  start. The README lists them; add new ones there.

## How text gets in

- floatkey types through the virtual-keyboard protocol with its own keymap.
- Superwhisper dictation does not go through floatkey. It commits through its
  own fcitx5 addon into whichever input context has fcitx5's focus. Text that
  "isn't typed" is usually focus held elsewhere: often a Qt process run
  without `QT_IM_MODULE=none`. To see which context has `focus:1`, run
  `busctl --user call org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1 DebugInfo`.
- Superwhisper's recent dictations are in
  `~/.local/share/superwhisper/recordings/<ts>/meta.json`; `linux.delivery`
  says how the text was sent.

## The tap rule

- With a touch screen, a field that takes focus by itself (a window that
  just opened) doesn't bring the keyboard up. Only a tap inside the focused
  window does.
- `src/taps.*` reads touches straight from libinput. This needs the user in
  the `input` group, and nothing is grabbed.
- `src/main.cpp` maps the panel point through Hyprland's touch transform.
  Launcher taps are told apart by timing (`TapWaits`, `TapOpens` in
  `controller.cpp`), because stage and the bar are full-screen layers.
- The `autoShowAfterTap` setting turns it off.
