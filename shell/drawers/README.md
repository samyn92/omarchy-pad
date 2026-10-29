# Drawers

Two drawers that pull down from the top edge, as on an iPad:

- **On the left, notifications:** what is on screen now and what came in
  over the last day, newest first, drawn as Omarchy's own notification cards.
  Earlier ones from the same app stack up, as Omarchy's toasts do; tap a stack
  to open it, and "Show less" to fold it again. Swipe a card sideways to take
  it away (a stack goes whole): let go past about a third of the way, or
  flick. One still on screen is dismissed in Omarchy too. Do Not Disturb is
  at the top, "Dismiss all" clears the ones on screen, and "Clear" the
  earlier ones.

  A notification whose sender names no icon (Claude's, Grok Bot's) wears the
  icon of the app installed under that name.

  Omarchy keeps no way to remove one notification from its history, so the
  drawer remembers the ones you took away (in
  `~/.local/state/omarchy-pad/hidden-notifications.json`) and leaves them
  out; Omarchy's own history still has them until it lets them go after a
  day.
- **On the right, quick settings,** laid out for a finger, each thing once:
  - Wi-Fi and Bluetooth as wide tiles saying what they are on with: a tap
    turns one on or off, its arrow opens Omarchy's panel (networks, devices).
  - Brightness and volume as bars as tall as a fingertip, which a finger
    moves by as much as it moves, from wherever it lands. Beside each,
    Omarchy's display and sound output panels.
  - A grid of four across: Focus (Do Not Disturb), Night Light, Stay Awake
    and rotation lock; Screenshot (the whole screen) and Region; the
    on-screen keyboard and Omarchy's clipboard history; Omarchy's theme and
    background pickers, and Text and QR code, which read them off an area of
    the screen. A screenshot or capture starts once the drawer is up and
    away.
  - The battery, and Omarchy's power profile (Saver, Balanced, Fast).

  Pull on down past it (from the bar, or anywhere on the drawer: a finger
  going up or down moves the drawer, whatever control it lands on) and the
  page turns to Omarchy's agents panel (the one its bar opens from the
  robot): your subscriptions' limits, this week's tokens, and the starter
  prompts. Push up on it to turn back, or tap the dots on the right edge.
  The drawer opens on quick settings again next time. The panel is
  Omarchy's own, shown in the drawer; typing in it (naming an account,
  pasting a sign-in code) needs Omarchy's own panel, as the drawer
  takes no keyboard.

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

`tests/try --check` opens the notifications on a few made-up ones, swipes and
taps them with finger events, says what passed, and closes.
