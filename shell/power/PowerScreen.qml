import Quickshell
import Quickshell.Wayland
import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The power screen: Power off, Restart and Lock as big round buttons over the dimmed desktop, opened by holding the
// tablet's power button (omarchy-pad-power, in the tablet session). A tap outside them, Esc, or another press of the
// power button closes it. The actions are Omarchy's, as its power menu runs them.
Item {
  id: root

  property string omarchyPath: Quickshell.env("OMARCHY_PATH") || "/usr/share/omarchy"
  property var shell: null
  property var manifest: null

  property bool opened: false
  property string fontFamily: Style.font.menuFamily

  property color foreground: Commons.Color.menu.text
  property color accent: Commons.Color.accent
  property color urgent: Commons.Color.urgent
  property color scrim: Commons.Color.background

  readonly property bool portrait: panel.height > panel.width
  // Big enough for a thumb on a tablet held in both hands, small enough for three in a row on a phone.
  readonly property int buttonSize: Math.max(Style.space(64), Math.min(Style.space(96),
    (Math.min(panel.width, panel.height) - Style.space(48)) / 4))

  readonly property var actions: [
    { id: "poweroff", label: "Power off", glyph: 0xF0425, command: ["bin/omarchy-system", "shutdown"], danger: true },
    { id: "restart", label: "Restart", glyph: 0xF0709, command: ["bin/omarchy-system", "reboot"], danger: false },
    { id: "lock", label: "Lock", glyph: 0xF033E, command: ["bin/omarchy-system-lock"], danger: false }
  ]

  function open(payloadJson) {
    root.opened = true
    Qt.callLater(function() { keys.forceActiveFocus() })
  }

  function close() {
    root.opened = false
  }

  function dismiss() {
    root.opened = false
    if (root.shell && typeof root.shell.hide === "function")
      root.shell.hide((root.manifest && root.manifest.id) || "omarchy-pad.power")
  }

  function toggle() {
    if (root.opened) root.dismiss()
    else root.open("{}")
  }

  // omarchy-pad-power asks, so that a short press closes the screen rather than putting the tablet to sleep.
  function isOpen(arg) {
    return root.opened ? "true" : "false"
  }

  function run(action) {
    root.dismiss()
    var command = action.command.slice()
    command[0] = root.omarchyPath + "/" + command[0]
    Quickshell.execDetached(command)
  }

  function overlayWindow() {
    return panel
  }

  OverlayWindow {
    id: panel
    shown: root.opened
    WlrLayershell.namespace: "omarchy-pad-power"

    Item {
      id: keys
      anchors.fill: parent
      focus: true
      Keys.onEscapePressed: root.dismiss()
    }

    Rectangle {
      id: dim
      anchors.fill: parent
      color: root.scrim
      opacity: panel.contentRevealed ? 0.82 : 0
      Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }

    MouseArea {
      anchors.fill: parent
      onClicked: root.dismiss()
    }

    Grid {
      id: buttons
      anchors.centerIn: parent
      columns: root.portrait ? 1 : root.actions.length
      rowSpacing: Style.space(28)
      columnSpacing: Style.space(48)
      opacity: panel.contentRevealed ? 1 : 0
      scale: panel.contentRevealed ? 1 : 0.92
      Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
      Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }

      Repeater {
        model: root.actions

        delegate: Item {
          id: button
          required property var modelData
          readonly property color tint: modelData.danger ? root.urgent : root.foreground
          objectName: "power-" + modelData.id
          width: root.buttonSize + Style.space(24)
          height: circle.height + Style.space(10) + label.implicitHeight

          Rectangle {
            id: circle
            width: root.buttonSize
            height: root.buttonSize
            radius: width / 2
            anchors.horizontalCenter: parent.horizontalCenter
            color: Qt.rgba(button.tint.r, button.tint.g, button.tint.b, touch.pressed ? 0.34 : 0.14)
            border.width: Math.max(1, Style.space(2))
            border.color: Qt.rgba(button.tint.r, button.tint.g, button.tint.b, 0.55)
            scale: touch.pressed ? 0.94 : 1
            Behavior on scale { NumberAnimation { duration: 90 } }
            Behavior on color { ColorAnimation { duration: 90 } }

            Text {
              anchors.centerIn: parent
              text: String.fromCodePoint(button.modelData.glyph)
              color: button.tint
              font.family: root.fontFamily
              font.pixelSize: Math.round(root.buttonSize * 0.42)
            }
          }

          Text {
            id: label
            anchors.top: circle.bottom
            anchors.topMargin: Style.space(10)
            anchors.horizontalCenter: parent.horizontalCenter
            text: button.modelData.label
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.heading
          }

          MouseArea {
            id: touch
            anchors.fill: parent
            onClicked: root.run(button.modelData)
          }
        }
      }
    }

    Text {
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: parent.bottom
      anchors.bottomMargin: Style.space(40)
      text: "Tap anywhere else to go back"
      color: root.foreground
      opacity: panel.contentRevealed ? 0.55 : 0
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
    }
  }
}
