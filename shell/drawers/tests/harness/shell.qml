import QtQuick
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

  Drawers {
    id: drawers
    omarchyPath: Quickshell.env("OMARCHY_PATH") || "/usr/share/omarchy"
    manifest: ({ id: "omarchy-pad.drawers" })
    shell: QtObject {
      property QtObject bar: QtObject {
        property bool barHidden: false
        property int barSize: harness.barSize
        property string position: "top"
        function barSizeFor(screenName) { return harness.barSize }
      }
      function hide(id) { return true }
    }
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

  IpcHandler {
    target: "harness"

    function state(): string {
      var settings = drawers.quickSettings
      return JSON.stringify({
        side: drawers.side,
        reveal: Math.round(drawers.reveal),
        openHeight: Math.round(drawers.openHeight),
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
