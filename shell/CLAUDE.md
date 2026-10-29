# shell

Omarchy shell plugins, one per folder, installed by `bin/install` (every
folder with a `manifest.json`; see the README for what is left out).

- `bar/` is a bar option of its own (kind `bar`), not a copy of Omarchy's bar:
  Omarchy updates must never break it. It hosts Omarchy's bar widgets from the
  registry the shell hands every bar, and gives them the part of Omarchy's
  `Bar.qml` they read (`foreground`, `run`, `requestPopout`, `registerClickTarget`,
  `moduleWidgets`...) and what the host calls on `shell.bar` (`summonBarWidget`,
  `hideBarWidget`, `isBarWidgetOpen`, `panelWidgetIdAt`, `barSize`, `barHidden`).
  When a widget needs more of Omarchy's bar, add it to `Bar.qml` rather than
  patching the widget.
- The pull is a `DragHandler` on the strip, under the widgets. Laid over them it
  swallows mouse presses (touch still gets through). Closing a panel's popup
  while a finger is down ends the touch, so an open panel closes when the pull
  ends.
- The bar calls the drawers in process, through `IpcRegistry.call` on the
  `omarchy-pad.drawers` target. The middle third's console pull goes to stage
  over its command socket (`stage top begin|follow|end`, from `consoleBegin`,
  `consoleFollow`, `consoleEnd`); without stage, Omarchy's toggle. The bar
  keeps retrying the socket while it's apart, and says `bar here` once
  connected, which makes stage drop its own strip along the top edge.
- It leaves the `omarchy.bar` IPC target to Omarchy's bar (which loads at
  every shell start before the selected one) and watches the `bar-off` toggle
  file alone. The empty-space tap runs `omarchy-bar background`, and the look
  to come back to is kept in `$XDG_STATE_HOME/omarchy-pad/bar-look`.
- `picker/` is a clone of `omarchy.image-picker` (`clonedFrom` in its
  manifest), so the shell sends every summon of Omarchy's picker to it. It
  isn't a copy: its `ImagePicker.qml` builds on Omarchy's live one, imported
  from `@OMARCHY_PATH@` (which `bin/install` fills in), and adds the swipe on a
  layer it puts in the picker's overlay window, found by the window's
  `shownKeyboardFocus`. Only `selectAdjacent`, `opened`, `layoutSettled` and
  `deleteConfirmOpen` of Omarchy's are relied on; without the window it still
  works, unswiped. Try it with `picker/tests/try` (`--check`), where
  `omarchy-theme-set` only logs.
- `bin/install` restarts the shell: the plugin reloads it triggers while files
  change can leave the bar without its widgets (no error is logged; a fresh
  start always loads them).
- Try it only with `bar/tests/try` (`--check` runs the taps and pulls and
  exits). Never run Omarchy's full shell nested: it writes the real
  `shell.json` and takes the session's notifications.
- `bin/install` with no `--to` switches the user's real shell to these
  plugins. That counts as an install: ask first.
