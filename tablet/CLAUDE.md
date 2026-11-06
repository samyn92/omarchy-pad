# tablet

The tablet session for Omarchy's Hyprland, packaged as `omarchy-pad-tablet`
(`pkgbuild/`, installed with `bin/install`).

`bin/omarchy-pad-tablet-setup` puts it in a user's Hyprland config:
- writes `~/.config/hypr/omarchy_pad_tablet.lua` from `hypr/omarchy_pad_tablet.lua`
  with the panel and scale, and writes it again when the template changes;
- adds a `require` to `hyprland.lua`, the first time only (a user who takes it
  out keeps it out), and turns the old `iceland_tablet` line into this one;
- adds `--disable-features=OzoneBubblesUsePlatformWidgets` to every
  Chromium-family `*-flags.conf` the user has (Chromium, Brave, Chrome,
  Vivaldi, Edge, Helium...), at every login, and makes the flags file for an
  installed browser that has none (Helium: Omarchy only writes them for the
  browsers its menu installs). Without it these browsers draw extension
  popups (Bitwarden) as subsurfaces and never enable text input for their
  fields, so floatkey never hears of them; a page's fields were never affected.
  Every login, because Omarchy migrations rewrite these files (`cp -f`).
  `tests/browser-flags` checks it.
`omarchy-pad-tablet-setup.service` runs it with `--first-login` at every login,
before the session starts, so no one has to run anything after installing.

`bin/omarchy-pad-tablet-session` (one per login, behind a flock):
- follows the accelerometer and rotates the panel, the touch screen and the
  pen together; while the session is locked, each turn writes the panel's mode
  the other way (`preferred`, or the mode spelled out), because Hyprland 0.57
  resizes the lock screen only on a mode change, and a turn alone left it in
  its old shape (`tests/rotation` checks it against a fake hyprctl);
- starts `floatkey --hidden` (falling back to wvkbd) and `stage`.
- holds a `handle-power-key` inhibitor for the session: the session handles the
  power button, so a device layer that watches the key leaves it alone.
- waits (up to 30 s) until the touch screen is readable before starting
  floatkey and stage: the session can start before logind gives the user its
  access, and both gave up on touch for the whole session.

`bin/omarchy-pad-power`, bound to the power button and Volume Down in
`hypr/omarchy_pad_tablet.lua` (also on the lock screen) in place of Omarchy's
power menu. The button is the only thing that decides what its press does:
the tablet session turns off Hyprland's `key_press_enables_dpms`, and the
script acts on the screen state as it was at the press.
- a short press with the screen on: backlight to 0 at once, the lock up behind
  it, then the screen off and the brightness put back (Hyprland lights the
  screen when a lock appears, so DPMS alone showed a glow); the tablet sleeps
  after 30 s of dark screen, so a press within that time lights it at once
  (the Pad 4 takes 1.3 s to wake from sleep, nearly all of it its Wi-Fi);
- a short press with the screen off lights it and tells the lock screen to
  wake as a touch does (a file `lock/LockView.qml` watches), so its countdown
  to blank starts again;
- the press that woke the tablet only lights the screen: the kernel's suspend
  count went up since the script sent it to sleep (never the journal, which
  lagged and sent the tablet straight back to sleep);
- held 0.6 s it opens the power screen (`omarchy-pad.power` in `shell/`, else
  Omarchy's power menu); a press while it is open closes it;
- with Volume Down, a full-screen screenshot; Volume Down alone, the volume.
`tests/power-button` runs it against fake commands.

Settings (`lib/tablet-conf`): PANEL, SCALE, ROTATE_FLIP, from the device
layer's `/usr/share/omarchy-pad/tablet.conf.d/*.conf`, then
`/etc/omarchy-pad/tablet.conf`, then `~/.config/omarchy-pad/tablet.conf`. With
no PANEL set it takes the first connected DSI or eDP connector.

- Nothing here knows a particular tablet. A device package says what its panel
  needs (iceland-device ships the OnePlus Pad 4's `50-oneplus-pad4.conf`).
  Panel quirks, audio and charging belong to the device layer, not here.
- The touch screen is opened by stage and floatkey through
  `70-omarchy-pad-touch.rules` (uaccess, for the user at the screen), not the
  `input` group.
- The setup edits the user's Hyprland config. Run by hand it reloads Hyprland:
  treat that like an install and ask first. Try changes to
  `omarchy_pad_tablet.lua` in a nested Hyprland before they reach the real
  config.
