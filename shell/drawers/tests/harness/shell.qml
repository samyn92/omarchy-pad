import QtQuick
import QtTest
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import "plugins/drawers"

// The drawers plugin in a nested Hyprland, wired the way the shell's panel
// loader wires it, under a stand-in for Omarchy's bar. It also serves the
// shell's socket, so a client that speaks omarchy-shell's protocol (stage's)
// can be tried against it.
ShellRoot {
  id: harness

  // As tall as Omarchy's bar on the tablet; the drawers come out from under it.
  readonly property int barSize: 30

  Variants {
    model: Quickshell.screens

    PanelWindow {
      required property var modelData
      screen: modelData
      anchors { top: true; left: true; right: true }
      implicitHeight: harness.barSize
      color: Commons.Color.bar.background
      WlrLayershell.namespace: "drawers-try-bar"

      Text {
        anchors.centerIn: parent
        text: "bar stand-in · pull left for notifications, right for quick settings"
        color: Commons.Color.bar.text
        font.family: Style.font.family
        font.pixelSize: Style.font.body
      }
    }
  }

  // Loaded as the shell's panel loader loads it: asynchronously, and handed
  // its properties once it is made (in onLoaded), not as it is made.
  readonly property var drawers: drawersLoader.item
  Loader {
    id: drawersLoader
    asynchronous: true
    source: "plugins/drawers/Drawers.qml"
    onLoaded: {
      // tests/try --check's own notifications, and its own memory of the
      // removed ones, in place of the session's.
      var dir = Quickshell.env("DRAWERS_TRY_NOTIFICATIONS")
      if (dir) {
        item.notifications.hiddenFile = Quickshell.env("DRAWERS_TRY_STATE") + "/hidden-notifications.json"
        item.notifications.stateDir = dir
      }
      item.omarchyPath = Quickshell.env("OMARCHY_PATH") || "/usr/share/omarchy"
      item.shell = hostShell
      item.manifest = ({ id: "omarchy-pad.drawers" })
    }
  }

  QtObject {
    id: hostShell
    property QtObject bar: QtObject {
      property bool barHidden: false
      property int barSize: harness.barSize
      property string position: "top"
      function barSizeFor(screenName) { return harness.barSize }
    }
    function hide(id) { return true }
  }

  // shell.qml's socket server, line for line.
  SocketServer {
    active: Quickshell.env("DRAWERS_TRY_SOCKET") !== ""
    path: Quickshell.env("DRAWERS_TRY_SOCKET")

    handler: Socket {
      id: connection

      parser: SplitParser {
        splitMarker: "\u001e"
        onRead: function(data) {
          var fields = String(data).split("\u001f")
          var result = fields.length >= 2 ? IpcRegistry.call(fields[0], fields[1], fields.slice(2)) : { ran: false }
          connection.write(result.ran ? "OK\u001f" + result.output + "\u001e" : "SKIP\u001e")
          connection.flush()
          connection.connected = false
        }
      }
    }
  }

  // The notification cards on screen, top first, each with its middle in the
  // drawer window and how many notifications it holds.
  function cards() {
    var out = []
    var windows = drawers.windows || []
    function visit(item, root) {
      if (!item || !item.visible) return
      if (item.objectName === "notificationCard" && item.height > 0) {
        var middle = item.mapToItem(root, item.width / 2, item.height / 2)
        out.push({ item: root, x: middle.x, y: middle.y, width: item.width, rows: item.rows.length,
                   drawn: item.frontHeight > 0 })
      }
      for (var i = 0; i < item.children.length; i++) visit(item.children[i], root)
    }
    for (var w = 0; w < windows.length; w++) visit(windows[w].contentItem, windows[w].contentItem)
    out.sort(function(a, b) { return a.y - b.y })
    return out
  }

  // The first item by objectName in the drawer windows.
  function find(name) {
    var windows = drawers.windows || []
    function visit(item) {
      if (!item) return null
      if (item.objectName === name) return item
      for (var i = 0; i < item.children.length; i++) {
        var found = visit(item.children[i])
        if (found) return found
      }
      return null
    }
    for (var w = 0; w < windows.length; w++) {
      var found = visit(windows[w].contentItem)
      if (found) return found
    }
    return null
  }

  TestEvent { id: events }
  property var pointer: null
  property var pendingMoves: []
  property bool releasePending: false

  function lift() {
    var p = harness.pointer
    harness.releasePending = false
    if (!p) return
    p.touch.release(0, p.item, p.x, p.y).commit()
    harness.pointer = null
  }

  Timer {
    interval: 16
    repeat: true
    running: harness.pendingMoves.length > 0
    onTriggered: {
      var moves = harness.pendingMoves.slice()
      var move = moves.shift()
      var p = harness.pointer
      if (p) {
        p.x = move.x
        p.y = move.y
        p.touch.move(0, p.item, p.x, p.y).commit()
      }
      harness.pendingMoves = moves
      if (moves.length === 0 && harness.releasePending) harness.lift()
    }
  }

  IpcHandler {
    target: "touch"

    function notifications(): string {
      var feed = drawers.notifications
      return JSON.stringify({
        live: feed.live.length,
        recent: feed.recent.length,
        hidden: Object.keys(feed.hidden).length,
        icons: feed.allRecent.map(function(row) { return row.appIcon }),
        cards: harness.cards().map(function(c) { return c.rows }),
        drawn: harness.cards().every(function(c) { return c.drawn })
      })
    }

    // A finger landing on the Nth notification card (from the top).
    function press(card: int): bool {
      var c = harness.cards()[card]
      if (!c) return false
      var p = { item: c.item, x: c.x, y: c.y, width: c.width }
      p.touch = events.touchEvent(c.item)
      p.touch.press(0, p.item, p.x, p.y).commit()
      harness.pointer = p
      return true
    }

    // Moves the finger by dx, dy over ms milliseconds, a step a frame.
    function moveBy(dx: real, dy: real, ms: int): bool {
      var p = harness.pointer
      if (!p) return false
      var steps = Math.max(1, Math.round(ms / 16))
      var last = harness.pendingMoves.length ? harness.pendingMoves[harness.pendingMoves.length - 1] : { x: p.x, y: p.y }
      var moves = harness.pendingMoves.slice()
      for (var i = 1; i <= steps; i++) moves.push({ x: last.x + dx * i / steps, y: last.y + dy * i / steps })
      harness.pendingMoves = moves
      return true
    }

    function release(): bool {
      if (!harness.pointer) return false
      if (harness.pendingMoves.length > 0) harness.releasePending = true
      else harness.lift()
      return true
    }

    // What the Clear button does.
    function clearEarlier(): bool {
      drawers.notifications.hide(drawers.notifications.allRecent)
      return true
    }
  }

  IpcHandler {
    target: "harness"

    function state(): string {
      var settings = drawers.quickSettings
      var agents = harness.find("agentsPage")
      return JSON.stringify({
        side: drawers.side,
        reveal: Math.round(drawers.reveal),
        openHeight: Math.round(drawers.openHeight),
        page: drawers.page,
        turn: Math.round(drawers.turn * 100) / 100,
        agents: agents ? { content: !!agents.content, height: Math.round(agents.implicitHeight) } : null,
        settling: drawers.settling,
        live: drawers.notifications.live.length,
        recent: drawers.notifications.recent.length,
        settings: {
          brightness: settings.brightness, volume: settings.volume, muted: settings.muted,
          wifi: settings.wifi, bluetooth: settings.bluetooth, dnd: settings.dnd,
          nightLight: settings.nightLight, stayAwake: settings.stayAwake,
          rotationLocked: settings.rotationLocked, keyboardShown: settings.keyboardShown
        }
      })
    }

    // Throws a switch the way a tap on it does. Wi-Fi is left out: it is the
    // real NetworkManager's.
    function toggle(name: string): string {
      var s = drawers.quickSettings
      if (name === "bluetooth") s.setBluetooth(!s.bluetooth)
      else if (name === "dnd") s.setDnd(!s.dnd)
      else if (name === "nightlight") s.setNightLight(!s.nightLight)
      else if (name === "stayawake") s.setStayAwake(!s.stayAwake)
      else if (name === "rotationlock") s.setRotationLocked(!s.rotationLocked)
      else if (name === "keyboard") s.setKeyboardShown(!s.keyboardShown)
      else return "unknown"
      return "ok"
    }

    function slide(name: string, percent: string): string {
      if (name === "brightness") drawers.quickSettings.setBrightness(Number(percent))
      else if (name === "volume") drawers.quickSettings.setVolume(Number(percent))
      else return "unknown"
      return "ok"
    }
  }
}
