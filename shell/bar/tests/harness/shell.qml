import QtQuick
import QtTest
import Quickshell
import Quickshell.Io
import qs.Commons
import "plugins/bar"

// Runs the bar plugin in a nested Hyprland with Omarchy's own widgets, as the
// shell would hand them over, and a stand-in for the drawers plugin that
// records what the bar asks of it. A fake host shell records anything a
// widget would write to shell.json. The press/moveBy/release calls feed the
// bar the same Qt events a mouse or a finger would, through Qt's test events.
ShellRoot {
  id: harness

  // The widgets' entry points, under the symlinked omarchy-plugins.
  readonly property var widgetFiles: ({
    "omarchy.clock": "panels/clock/BarWidget.qml",
    "omarchy.workspaces": "bar/widgets/Workspaces.qml",
    "omarchy.indicators": "bar/widgets/Indicators.qml",
    "omarchy.network": "panels/network/Panel.qml",
    "omarchy.bluetooth": "panels/bluetooth/Panel.qml",
    "omarchy.audio": "panels/audio/Panel.qml",
    "omarchy.power": "panels/power/Panel.qml"
  })
  property var widgets: ({})
  property var writes: []
  property var drawerCalls: []
  property real drawerHeight: 400
  // The press in progress: where, and whether a finger made it.
  property var pointer: null
  // Moves still to be sent, one per frame, so a pull has a real speed, and
  // whether the press lifts right after the last of them.
  property var pendingMoves: []
  property bool releasePending: false

  QtObject {
    id: registry
    property var widgets: harness.widgets
    property int revision: 0
    function metadataFor(id) { return widgets[id] ? widgets[id].metadata : null }
  }

  QtObject {
    id: host
    function mutateShellConfig(mutator) { harness.writes = harness.writes.concat(["mutate"]) }
    function updateEntryInline(id, settings) { harness.writes = harness.writes.concat(["entry " + id]) }
    function summon(id, payload) { return false }
    function hide(id) { return false }
    function toggle(id, payload) { return false }
  }

  Bar {
    id: bar
    omarchyPath: Quickshell.env("OMARCHY_PATH")
    barWidgetRegistry: registry
    barConfig: ({ position: "top", background: "opaque", layout: {} })
    shell: host
  }

  Component.onCompleted: {
    var loaded = {}
    for (var id in widgetFiles) {
      var component = Qt.createComponent(Qt.resolvedUrl("omarchy-plugins/" + widgetFiles[id]))
      if (component.status === Component.Error) console.warn("harness: " + id + ": " + component.errorString())
      else loaded[id] = { component: component, metadata: { firstParty: true } }
    }
    widgets = loaded
  }

  // The real drawers plugin, when tests/try was given one; otherwise the
  // stand-in below answers for it.
  readonly property bool realDrawers: Quickshell.env("BAR_TRY_DRAWERS") === "1"
  Loader {
    active: harness.realDrawers
    source: "RealDrawers.qml"
    onLoaded: item.shell = { bar: bar }
  }

  ShellIpc {
    target: "omarchy-pad.drawers"
    enabled: !harness.realDrawers

    function begin(side: string): string {
      harness.drawerCalls = harness.drawerCalls.concat(["begin " + side])
      return String(harness.drawerHeight)
    }
    function follow(px: string): void { harness.drawerCalls = harness.drawerCalls.concat(["follow " + px]) }
    function settle(open: string): void { harness.drawerCalls = harness.drawerCalls.concat(["settle " + open]) }
    function close(): void { harness.drawerCalls = harness.drawerCalls.concat(["close"]) }
    function state(): string { return "closed" }
  }

  TestEvent { id: events }

  function hostNamed(name) {
    var hosts = bar.widgetHosts
    for (var i = 0; i < hosts.length; i++)
      if (hosts[i].moduleName === name && hosts[i].activeItem && hosts[i].visible) return hosts[i]
    return null
  }

  // The middle of a widget, or "x:SHARE": that far along the bar, halfway
  // down it.
  function pointFor(where) {
    var clock = hostNamed("omarchy.clock")
    if (!clock || !clock.Window.window) return null
    if (where.indexOf("x:") === 0) {
      var root = clock.Window.window.contentItem
      return { item: root, x: root.width * Number(where.slice(2)), y: bar.barSize / 2 }
    }
    var widget = hostNamed(where)
    return widget ? { item: widget, x: widget.width / 2, y: widget.height / 2 } : null
  }

  function sendMove(move) {
    var p = harness.pointer
    if (!p) return
    p.x = move.x
    p.y = move.y
    if (p.finger) {
      p.touch.move(0, p.item, p.x, p.y)
      if (p.second) p.touch.move(1, p.item, p.x + 60, p.y)
      p.touch.commit()
    } else events.mouseMove(p.item, p.x, p.y, -1, Qt.LeftButton, Qt.NoModifier)
  }

  function lift() {
    var p = harness.pointer
    harness.releasePending = false
    if (!p) return
    if (p.finger) {
      p.touch.release(0, p.item, p.x, p.y)
      if (p.second) p.touch.release(1, p.item, p.x + 60, p.y)
      p.touch.commit()
    } else {
      events.mouseRelease(p.item, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
    }
    harness.pointer = null
  }

  Timer {
    interval: 16
    repeat: true
    running: harness.pendingMoves.length > 0
    onTriggered: {
      var moves = harness.pendingMoves.slice()
      harness.sendMove(moves.shift())
      harness.pendingMoves = moves
      if (moves.length === 0 && harness.releasePending) harness.lift()
    }
  }

  IpcHandler {
    target: "harness"

    function ready(): bool {
      return !!harness.hostNamed("omarchy.clock") && !!harness.hostNamed("omarchy.power")
    }

    function instance(): string {
      return Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE") || ""
    }

    function press(where: string, finger: bool): bool {
      var p = harness.pointFor(where)
      if (!p) return false
      p.finger = finger
      if (finger) {
        p.touch = events.touchEvent(p.item)
        p.touch.press(0, p.item, p.x, p.y).commit()
      } else {
        events.mousePress(p.item, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
      }
      harness.pointer = p
      return true
    }

    // Moves the press in progress by dx, dy over ms milliseconds, a step a
    // frame as a real finger reports it. Returns at once.
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

    // A second finger beside the first.
    function addFinger(): bool {
      var p = harness.pointer
      if (!p || !p.finger) return false
      p.touch.stationary(0).press(1, p.item, p.x + 60, p.y).commit()
      p.second = true
      return true
    }

    // Lets go, after any moves still to be sent.
    function release(): bool {
      if (!harness.pointer) return false
      if (harness.pendingMoves.length > 0) harness.releasePending = true
      else harness.lift()
      return true
    }

    function clearCalls(): void { harness.drawerCalls = [] }

    function state(): string {
      function opened(name) {
        var widget = harness.hostNamed(name)
        return !!(widget && widget.activeItem.opened)
      }
      var calls = harness.drawerCalls
      return JSON.stringify({
        calls: calls,
        begun: calls.filter(c => c.indexOf("begin") === 0),
        settled: calls.filter(c => c.indexOf("settle") === 0),
        follows: calls.filter(c => c.indexOf("follow") === 0).length,
        writes: harness.writes,
        clockOpen: opened("omarchy.clock"),
        powerOpen: opened("omarchy.power"),
        widgets: bar.debugBarGeometry()
      })
    }
  }
}
