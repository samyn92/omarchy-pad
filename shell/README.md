# Omarchy Pad shell plugins

Plugins for Omarchy's shell that make it work by touch. Each folder here is
one plugin.

## The bar (`omarchy-pad.bar`)

A status bar for the tablet, in place of Omarchy's bar. The time and date are
on the left, the workspaces in the middle, and the status icons on the right:
Omarchy's indicators (Do Not Disturb, night light...), Wi-Fi, Bluetooth, volume,
display brightness and battery. The bar always stays at the top, and nothing on
it can be dragged around or off.

The widgets are Omarchy's own, so a tap opens the same panels as on Omarchy's
bar: the calendar, the battery and power profiles, Wi-Fi networks, audio
devices, brightness. They follow the theme, and your own settings for them from
Omarchy's bar layout come along. The clock always shows the time and the date, and the
battery shows its percentage unless you've turned that off.

Pull down from the bar to open a drawer:

- from the left third, notifications;
- from the right third, quick settings;
- from the middle, the scratchpad console, following the finger (stage draws
  it; without stage, Omarchy's own toggle). A push up from the bottom, or on
  the dimmed screen, takes it back up.

The drawer follows your finger. Let go past about a third of the way down, or
flick, and it opens; otherwise it goes back up. A mouse can pull them down too.
The drawers come from the `omarchy-pad.drawers` plugin; without it, pulls from
the sides do nothing.

Tap the bar's empty space to make it clear, and tap again for the look it had
before, opaque or glassy. It is Omarchy's own bar background
(`omarchy bar background`), so the setting is the same one Omarchy's bar uses.
`omarchy toggle bar` hides the bar as it hides Omarchy's.

## The picker (`omarchy-pad.image-picker`)

Omarchy's theme and background picker, which a finger can swipe through:
swipe sideways and the pictures go by, one for every stretch of the finger,
and a flick carries on a few more. Tap a picture beside the big one to bring
it to the middle, and the big one to use it. It is Omarchy's own picker, loaded
from where Omarchy keeps it, so it looks and works as Omarchy's does and an
Omarchy update reaches it too. Installed, it takes the place of Omarchy's
(`omarchy plugin disable omarchy-pad.image-picker` goes back), wherever the
picker opens from: the Omarchy menu, its keys, or the quick settings drawer.

## Installing

```bash
shell/bin/install
```

This puts every plugin here in `~/.config/omarchy/plugins`, turns it on, which
makes `omarchy-pad.bar` the bar in use, and restarts the shell.
`shell/bin/install bar` installs just the bar. Omarchy updates never change these plugins: they only use what
Omarchy offers any bar.

To go back to Omarchy's bar:

```bash
omarchy bar reset
```

and `omarchy bar use omarchy-pad.bar` switches back again.

## Trying a change

`shell/bar/tests/try --check` runs the bar in a nested Hyprland window, with
Omarchy's widgets and a stand-in for the drawers. It taps and pulls it with
mouse and finger events and says what passed. Without `--check` it stays open
to try by hand.

## How a plugin here is built

A folder with a `manifest.json` is a plugin. Everything in it is installed
except `bin/`, `tests/`, `docs/`, `README.md` and `CLAUDE.md`.
