# Drawers

Two drawers that pull down from the top edge, as on an iPad:

- **On the left, notifications:** what is on screen now and what came in
  over the last day, newest first, drawn as Omarchy's own notification cards.
  Do Not Disturb is at the top, and "Dismiss all" clears the ones on screen.
  Recent notifications can't be removed one by one: Omarchy keeps no way to.
- **On the right, quick settings:** brightness and volume, switches for Wi-Fi,
  Bluetooth, Do Not Disturb, Night Light, Stay Awake, rotation lock and the
  on-screen keyboard, and buttons that open Omarchy's full Wi-Fi, audio,
  Bluetooth and display panels and its theme and background pickers.

Pull down from the bar with one finger, on its left or right third; the
drawer follows the finger and opens if you let go past a third of the way, or
with a flick. A tap beside it, or a push up, puts it away. Omarchy Pad's bar
does the pulling.

It is an Omarchy shell plugin, `omarchy-pad.drawers`, built from the shell's
own components, so it takes on the theme like any Omarchy panel.

## Commands

The bar drives a drawer from inside the shell. The same commands work from
outside it, through `omarchy-shell`:

```bash
omarchy-shell omarchy-pad.drawers begin right   # ready it closed; prints its open height
omarchy-shell omarchy-pad.drawers follow 240     # this many px open, under the finger
omarchy-shell omarchy-pad.drawers settle 1       # let go: open (0 closes)
omarchy-shell omarchy-pad.drawers close
omarchy-shell omarchy-pad.drawers state          # closed, left or right
```

`omarchy-shell shell summon omarchy-pad.drawers '{"side":"left"}'` opens one
without a finger.

## Trying it

`tests/try` opens a nested Hyprland with the drawers under a stand-in bar,
and prints the commands that drive it. Its switches run fakes that only log
(to `state/calls.log` in its work dir), except Wi-Fi, which is the real
NetworkManager's: leave that one alone there.
