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
  `omarchy-pad.drawers` target. The middle third's console pull has hook points
  (`consoleBegin`, `consoleFollow`, `consoleEnd`) for stage to take over.
- Try it only with `bar/tests/try` (`--check` runs the taps and pulls and
  exits). Never run Omarchy's full shell nested: it writes the real
  `shell.json` and takes the session's notifications.
- `bin/install` with no `--to` switches the user's real shell to these
  plugins. That counts as an install: ask first.
