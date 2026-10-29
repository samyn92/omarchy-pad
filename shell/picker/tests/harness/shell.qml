import QtQuick
import QtTest
import Quickshell
import Quickshell.Io
import "plugins/picker"

// Runs the picker plugin in a nested Hyprland, opened on the themes as
// Omarchy's menu opens it. The press/moveBy/release calls feed it the same Qt
// events a finger would, through Qt's test events.
ShellRoot {
  id: harness

  property var pointer: null
  property var pendingMoves: []
  property bool releasePending: false

  ImagePicker { id: picker }

  TestEvent { id: events }

  function surface() {
    var window = picker.overlayWindow()
    return window ? window.contentItem : null
  }

  function sendMove(move) {
    var p = harness.pointer
    if (!p) return
    p.x = move.x
    p.y = move.y
    p.touch.move(0, p.item, p.x, p.y).commit()
  }

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
      harness.sendMove(moves.shift())
      harness.pendingMoves = moves
      if (moves.length === 0 && harness.releasePending) harness.lift()
    }
  }

  IpcHandler {
    target: "harness"

    function ready(): bool { return !!harness.surface() }

    function openThemes(): void { picker.open('{"source":"themes"}') }

    function state(): string {
      var s = harness.surface()
      return JSON.stringify({
        opened: picker.opened,
        settled: picker.layoutSettled,
        count: picker.imageArray ? picker.imageArray.length : 0,
        selected: picker.selectedIndex,
        width: s ? s.width : 0,
        height: s ? s.height : 0
      })
    }

    // A finger landing at SHAREX, SHAREY of the window.
    function press(shareX: real, shareY: real): bool {
      var s = harness.surface()
      if (!s) return false
      var p = { item: s, x: s.width * shareX, y: s.height * shareY }
      p.touch = events.touchEvent(s)
      p.touch.press(0, p.item, p.x, p.y).commit()
      harness.pointer = p
      return true
    }

    // Moves the finger by dx over ms milliseconds, a step a frame as a real
    // finger reports it. Returns at once.
    function moveBy(dx: real, ms: int): bool {
      var p = harness.pointer
      if (!p) return false
      var steps = Math.max(1, Math.round(ms / 16))
      var last = harness.pendingMoves.length ? harness.pendingMoves[harness.pendingMoves.length - 1] : { x: p.x, y: p.y }
      var moves = harness.pendingMoves.slice()
      for (var i = 1; i <= steps; i++) moves.push({ x: last.x + dx * i / steps, y: last.y })
      harness.pendingMoves = moves
      return true
    }

    // Lets go, after any moves still to be sent.
    function release(): bool {
      if (!harness.pointer) return false
      if (harness.pendingMoves.length > 0) harness.releasePending = true
      else harness.lift()
      return true
    }
  }
}
