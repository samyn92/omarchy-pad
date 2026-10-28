# tablet

The tablet session for Omarchy's Hyprland. `install-tablet-setup.sh` sets it
up once:
- writes `~/.config/iceland/tablet.conf`;
- writes `~/.config/hypr/iceland_tablet.lua` from `hypr/iceland_tablet.lua`;
- adds a `require` to `hyprland.lua`;
- runs `hyprctl reload`.

`bin/iceland-tablet-session` (one per login, behind a flock):
- follows the accelerometer and rotates the panel, the touch screen and the
  pen together;
- starts `floatkey --hidden` (falling back to wvkbd) and `stage`.

- The setup script edits the user's live Hyprland config and reloads it. Treat
  running it like an install: ask first. Try changes to `iceland_tablet.lua`
  in a nested Hyprland before they reach the real config.
- The `iceland-*` names come from the OnePlus Pad's device layer. Rotation,
  the keyboard and gestures are Omarchy Pad's. Panel quirks, audio and
  charging belong to the device layer (iceland-device), not here.
- `wvkbd-0.20-1-aarch64.pkg.tar.xz` is a vendored fallback keyboard package.
