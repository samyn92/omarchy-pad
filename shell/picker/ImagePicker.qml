import QtQuick
import "file:@OMARCHY_PATH@/shell/plugins/image-picker" as Upstream

// Omarchy's image picker, the one that switches themes and backgrounds, with
// swiping added for a finger: it is Omarchy's own picker, loaded from where
// Omarchy keeps it, so an Omarchy update changes it here too. The keys and
// taps work as they always did; a swipe sideways goes through the pictures,
// one for every stretch of the finger, and a flick carries on a few more.
Upstream.ImagePicker {
  id: picker

  // How far a finger goes for the next picture: about the width of the
  // slices beside the big one, so the strip seems to follow it.
  readonly property real swipeStep: 90
  // A flick carries on one more picture for every this many px/s, up to
  // `flickMost`.
  readonly property real flickSpeed: 700
  readonly property int flickMost: 6
  readonly property bool mirrored: Qt.application.layoutDirection === Qt.RightToLeft

  // A finger going left brings the next picture in, as on a phone.
  function swiped(direction) {
    if (typeof picker.selectAdjacent === "function")
      picker.selectAdjacent(mirrored ? -direction : direction)
  }

  // The picker draws in an overlay window of its own; the touch layer goes on
  // top of it. It is found by what only that window has, so a picker laid out
  // otherwise after an Omarchy update still works, only without swiping.
  function overlayWindow() {
    for (var i = 0; i < picker.data.length; i++) {
      var candidate = picker.data[i]
      if (candidate && candidate.shownKeyboardFocus !== undefined && candidate.contentItem) return candidate
    }
    return null
  }

  Component.onCompleted: {
    var window = overlayWindow()
    if (window) swipeLayer.createObject(window.contentItem)
    else console.warn("omarchy-pad.image-picker: no overlay window to swipe in; taps and keys still work")
  }

  Timer {
    id: flick
    property int left: 0
    property int direction: 1
    interval: 70
    repeat: true
    onTriggered: {
      picker.swiped(direction)
      if (--left <= 0) stop()
    }
  }

  Component {
    id: swipeLayer

    // Over everything the picker draws, taking nothing until a finger has
    // moved: a tap still reaches the picture under it (picking it, or
    // applying the one picked) and the backdrop (closing the picker).
    Item {
      anchors.fill: parent
      z: 1000

      DragHandler {
        id: drag
        target: null
        yAxis.enabled: false
        enabled: picker.opened && picker.layoutSettled && !picker.deleteConfirmOpen
        // How much of the finger's way has turned into pictures, and how fast
        // it was last going.
        property real used: 0
        property real speed: 0

        onActiveChanged: {
          if (active) {
            flick.stop()
            used = 0
            speed = 0
            return
          }
          var extra = Math.min(picker.flickMost, Math.floor(Math.abs(speed) / picker.flickSpeed))
          if (extra > 0) {
            flick.direction = speed < 0 ? 1 : -1
            flick.left = extra
            flick.start()
          }
        }
        onActiveTranslationChanged: {
          speed = centroid.velocity.x
          var along = -activeTranslation.x
          while (along - used >= picker.swipeStep) {
            used += picker.swipeStep
            picker.swiped(1)
          }
          while (along - used <= -picker.swipeStep) {
            used -= picker.swipeStep
            picker.swiped(-1)
          }
        }
      }
    }
  }
}
