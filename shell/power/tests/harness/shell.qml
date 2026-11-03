import QtQuick
import QtTest
import Quickshell
import Quickshell.Io
import "plugins/power"

// Runs the power screen plugin in a nested Hyprland. Its actions run the fake Omarchy commands in FAKE_OMARCHY, which
// only log. tap() feeds it the Qt touch events a finger would.
ShellRoot {
  id: harness

  PowerScreen {
    id: power
    omarchyPath: Quickshell.env("FAKE_OMARCHY")
  }

  TestEvent { id: events }

  function surface() {
    var window = power.overlayWindow()
    return window && window.contentRevealed ? window.contentItem : null
  }

  function find(item, name) {
    if (!item) return null
    if (item.objectName === name) return item
    var children = item.children || []
    for (var i = 0; i < children.length; i++) {
      var found = harness.find(children[i], name)
      if (found) return found
    }
    return null
  }

  function touchAt(x, y) {
    var s = harness.surface()
    if (!s) return false
    var touch = events.touchEvent(s)
    touch.press(0, s, x, y).commit()
    touch.release(0, s, x, y).commit()
    return true
  }

  IpcHandler {
    target: "harness"

    function ready(): bool { return !!power.overlayWindow() }

    function open(): void { power.open("{}") }

    function state(): string {
      var s = harness.surface()
      var centers = {}
      for (var name of ["poweroff", "restart", "lock"]) {
        var b = harness.find(s, "power-" + name)
        if (b) {
          var p = b.mapToItem(s, b.width / 2, b.height / 2)
          centers[name] = { x: Math.round(p.x), y: Math.round(p.y), size: Math.round(b.width) }
        }
      }
      return JSON.stringify({ opened: power.opened, isOpen: power.isOpen(""), revealed: !!s,
                              width: s ? s.width : 0, height: s ? s.height : 0, buttons: centers })
    }

    // A finger tapping at SHAREX, SHAREY of the window.
    function tap(shareX: real, shareY: real): bool {
      var s = harness.surface()
      return !!s && harness.touchAt(s.width * shareX, s.height * shareY)
    }

    // A finger tapping the middle of a button: poweroff, restart, lock.
    function tapButton(name: string): bool {
      var s = harness.surface()
      var b = harness.find(s, "power-" + name)
      if (!b) return false
      var p = b.mapToItem(s, b.width / 2, b.height / 2)
      return harness.touchAt(p.x, p.y)
    }
  }
}
