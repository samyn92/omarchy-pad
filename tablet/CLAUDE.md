# tablet

The tablet session for Omarchy's Hyprland, packaged as `omarchy-pad-tablet`
(`pkgbuild/`, installed with `bin/install`).

`bin/omarchy-pad-tablet-setup` puts it in a user's Hyprland config:
- writes `~/.config/hypr/omarchy_pad_tablet.lua` from `hypr/omarchy_pad_tablet.lua`
  with the panel and scale, and writes it again when the template changes;
- adds a `require` to `hyprland.lua`, the first time only (a user who takes it
  out keeps it out), and turns the old `iceland_tablet` line into this one.
`omarchy-pad-tablet-setup.service` runs it with `--first-login` at every login,
before the session starts, so no one has to run anything after installing.

`bin/omarchy-pad-tablet-session` (one per login, behind a flock):
- follows the accelerometer and rotates the panel, the touch screen and the
  pen together;
- starts `floatkey --hidden` (falling back to wvkbd) and `stage`.
- holds a `handle-power-key` inhibitor for the session: the session handles the
  power button, so a device layer that watches the key leaves it alone.

`bin/omarchy-pad-power`, bound to the power button and Volume Down in
`hypr/omarchy_pad_tablet.lua` (also on the lock screen) in place of Omarchy's
power menu:
- a short press turns the screen off at once, then locks behind the dark
  screen and sleeps, so the lock screen is first seen on waking; where sleep is
  held off (a device layer keeping the tablet awake while it charges, Omarchy's
  suspend turned off) it stays locked with the screen off; with the screen off
  it turns it on; the press that woke the tablet (a kernel "PM: suspend exit"
  in the last 4 s) only turns the screen on;
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
