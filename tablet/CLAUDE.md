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
