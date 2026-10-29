import QtQuick
import Quickshell
import Quickshell.Hyprland
import qs.Commons

// Two drawers that pull down from the top edge, as on an iPad: notifications
// on the left, quick settings on the right. stage recognises the finger and
// drives a drawer while it pulls, through the commands below; once a drawer is
// open, it handles its own touches. Everything on it is built from Omarchy's
// own Ui components, so it looks like the rest of the shell and follows the
// theme.
Item {
  id: root

  // Handed over by the shell's panel loader.
  property string omarchyPath: ""
  property var shell: null
  property var manifest: null

  readonly property string pluginId: manifest && manifest.id ? String(manifest.id) : "omarchy-pad.drawers"

  // The drawer on screen or being pulled ("left" or "right"), or "" when
  // both are closed.
  property string side: ""
  property var activeScreen: null
  // How far that drawer is down, in logical px, from 0 to openHeight.
  property real reveal: 0
  readonly property var activeWindow: {
    var windows = drawerWindows.instances
    for (var i = 0; i < windows.length; i++)
      if (windows[i] && windows[i].modelData === root.activeScreen) return windows[i]
    return null
  }
  readonly property real openHeight: activeWindow ? activeWindow.openHeight : 0
  readonly property bool settling: settleAnimation.running

  readonly property QuickSettingsState quickSettings: QuickSettingsState {
    pluginDir: root.pluginDir
  }
  readonly property NotificationFeed notifications: NotificationFeed {
    omarchyPath: root.omarchyPath
  }

  readonly property string pluginDir: {
    var url = String(Qt.resolvedUrl("."))
    return url.indexOf("file://") === 0 ? decodeURIComponent(url.substring(7)).replace(/\/$/, "") : url
  }

  function focusedScreen() {
    var monitor = Hyprland.focusedMonitor
    var name = monitor ? String(monitor.name || "") : ""
    for (var i = 0; i < Quickshell.screens.length; i++)
      if (Quickshell.screens[i].name === name) return Quickshell.screens[i]
    return Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
  }

  // Readies a drawer at height 0 and answers how far it opens, so stage can
  // map the finger onto it. Its content is read again here: the drawer shows
  // what is true as it comes down, not what was true when it last closed.
  function begin(which) {
    if (which !== "left" && which !== "right") return "0"
    settleAnimation.stop()
    if (root.side && root.side !== which) root.reveal = 0
    root.activeScreen = root.focusedScreen()
    root.side = which
    root.reveal = 0
    if (which === "right") root.quickSettings.refresh()
    else root.notifications.refresh()
    return String(Math.round(root.openHeight))
  }

  // The finger's position, 1:1 and without animation.
  function follow(px) {
    if (!root.side) return
    settleAnimation.stop()
    root.reveal = Math.max(0, Math.min(root.openHeight, Number(px) || 0))
  }

  // The finger let go: all the way down or back up. The run takes longer the
  // further it has to go, so a nearly open drawer doesn't crawl the last bit.
  function settle(open) {
    if (!root.side) return
    var target = open ? root.openHeight : 0
    var distance = Math.abs(target - root.reveal)
    var full = Math.max(1, root.openHeight)
    settleAnimation.stop()
    settleAnimation.from = root.reveal
    settleAnimation.to = target
    settleAnimation.duration = Style.duration(Math.round(140 + 160 * Math.min(1, distance / full)))
    settleAnimation.start()
  }

  function close() {
    if (root.side && !(settleAnimation.running && settleAnimation.to === 0)) root.settle(false)
  }

  function state() {
    return root.side || "closed"
  }

  // The shell's own entry points: `shell summon omarchy-pad.drawers
  // '{"side":"right"}'` opens a drawer without a finger, and hiding the
  // plugin closes it.
  function open(payloadJson) {
    var payload = {}
    try { payload = JSON.parse(payloadJson || "{}") || {} } catch (e) {}
    var which = payload.side === "left" ? "left" : "right"
    root.begin(which)
    root.settle(true)
  }

  NumberAnimation {
    id: settleAnimation
    target: root
    property: "reveal"
    easing.type: Easing.Bezier
    easing.bezierCurve: Style.easeOut
    onFinished: {
      if (settleAnimation.to > 0) {
        // Fully open, the drawer keeps to its content's height as that
        // changes (a notification arriving, say).
        root.reveal = Qt.binding(function() { return root.openHeight })
        return
      }
      root.side = ""
      root.activeScreen = null
      // The shell keeps a summoned panel marked open until it is hidden.
      if (root.shell && typeof root.shell.hide === "function") root.shell.hide(root.pluginId)
    }
  }

  ShellIpc {
    target: "omarchy-pad.drawers"

    function begin(side: string): string { return root.begin(side) }
    function follow(px: string): void { root.follow(px) }
    function settle(open: string): void { root.settle(open === "1" || open === "true") }
    function close(): void { root.close() }
    function state(): string { return root.state() }
  }

  // A drawer window per screen (tests/try reaches into them).
  readonly property var windows: drawerWindows.instances

  Variants {
    id: drawerWindows
    model: Quickshell.screens

    delegate: DrawerWindow {
      drawers: root
    }
  }
}
