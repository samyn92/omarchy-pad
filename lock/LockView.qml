import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons

// Omarchy's lock view (UpstreamLockView.qml, copied in by bin/install) with
// floatkey's keyboard along the bottom while no hardware keyboard is attached.
// It types through the view's own password signals, so the service and its
// PAM flow are Omarchy's unchanged. The keyboard key in its place brings it
// up anyway, for a keyboard that is attached but folded away.
UpstreamLockView {
  id: root

  // Until the first look at the devices the keyboard stays down, so a desktop
  // never sees it flash up at lock.
  property bool keyboardKnown: false
  property bool hardwareKeyboard: false
  // What the user picked with the hide key or the keyboard key: "shown",
  // "hidden", or "" to follow the hardware. A device coming or going and the
  // next lock both hand it back to the hardware.
  property string keyboardChoice: ""
  readonly property bool keyboardOpen: inputEnabled && keyboardKnown
    && (keyboardChoice === "shown" || (keyboardChoice === "" && !hardwareKeyboard))

  // floatkey's layouts and its sizing for the docked keyboard.
  property var layoutCodes: ["us"]
  property int layoutIndex: 0
  readonly property var supportedLayouts: ["us", "de"]

  onHardwareKeyboardChanged: keyboardChoice = ""
  onInputEnabledChanged: if (!inputEnabled) keyboardChoice = ""

  // Same test as floatkey's KeyboardWatcher: a keyboard is an input device
  // with Q, A, Z, M and space, which leaves out power buttons, lid switches,
  // headset jacks and the virtual keyboards Hyprland makes. Those keys all sit
  // in the lowest word of the kernel's key bitmap.
  readonly property string sysInput: (Quickshell.env("OMARCHY_INPUT_ROOT") || "") + "/sys/class/input"
  property bool probeAgain: false

  Process {
    id: keyboardProbe
    command: ["bash", "-c",
      "for caps in \"$1\"/event*/device/capabilities/key; do "
        + "[[ -r $caps ]] || continue; read -ra words < \"$caps\"; (( ${#words[@]} )) || continue; "
        + "(( bits = 16#${words[-1]} )); "
        + "for key in 16 30 44 50 57; do (( (bits >> key) & 1 )) || continue 2; done; "
        + "echo yes; exit; done; echo no",
      "lock-keyboard-probe", root.sysInput]
    running: true
    stdout: StdioCollector {
      onStreamFinished: {
        root.hardwareKeyboard = text.trim() === "yes"
        root.keyboardKnown = true
      }
    }
    onExited: function(code) {
      if (code !== 0) {
        root.hardwareKeyboard = false
        root.keyboardKnown = true
      }
      if (root.probeAgain) {
        root.probeAgain = false
        running = true
      }
    }
  }

  Connections {
    target: InputDevices
    function onChanged() {
      if (keyboardProbe.running) root.probeAgain = true
      else keyboardProbe.running = true
    }
  }

  // The layouts floatkey has, picked the way floatkey picks them: its own
  // layouts setting, else Hyprland's input:kb_layout, keeping the ones it
  // knows. The lock starts in the one floatkey was last left in.
  Process {
    command: ["bash", "-c",
      "conf=${XDG_CONFIG_HOME:-$HOME/.config}/floatkey/floatkey.conf; "
        + "codes=$(sed -n 's/^layouts=//p' \"$conf\" 2>/dev/null); "
        + "[[ -n $codes ]] || codes=$(hyprctl getoption input:kb_layout -j 2>/dev/null | jq -r '.str // empty'); "
        + "printf '%s\\n%s\\n' \"$codes\" \"$(sed -n 's/^layoutIndex=//p' \"$conf\" 2>/dev/null)\""]
    running: true
    stdout: StdioCollector {
      onStreamFinished: {
        var lines = text.split("\n")
        var codes = []
        var listed = lines[0].replace(/"/g, "").split(",")
        for (var i = 0; i < listed.length; i++) {
          var code = listed[i].trim().toLowerCase()
          if (root.supportedLayouts.indexOf(code) !== -1 && codes.indexOf(code) === -1) codes.push(code)
        }
        root.layoutCodes = codes.length > 0 ? codes : ["us"]
        var index = parseInt(lines[1], 10)
        root.layoutIndex = isFinite(index) ? Math.max(0, Math.min(index, root.layoutCodes.length - 1)) : 0
      }
    }
  }

  LockKeyboard {
    id: keyboard
    objectName: "lockKeyboard"

    property real reveal: root.keyboardOpen ? 1 : 0
    Behavior on reveal { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }

    anchors.left: parent.left
    anchors.right: parent.right
    height: Math.round(0.3 * Math.min(root.width, root.height))
    y: root.height - height * reveal
    visible: reveal > 0
    enabled: root.keyboardOpen && !root.authenticatingPassword
    opacity: root.authenticatingPassword ? 0.6 : 1

    background: Commons.Color.background
    foreground: Commons.Color.foreground
    accent: Commons.Color.accent
    radius: Style.cornerRadius
    layoutCodes: root.layoutCodes
    layoutIndex: root.layoutIndex

    onTouched: root.wakeRequested()
    onTyped: function(text) { root.passwordTextEdited(root.passwordText + text) }
    onErased: root.passwordTextEdited(Array.from(root.passwordText).slice(0, -1).join(""))
    // What the field's own return does.
    onSubmitted: {
      var submitted = root.passwordText
      root.passwordTextEdited("")
      if (submitted.length > 0) root.submitPassword(submitted)
    }
    onHideRequested: root.keyboardChoice = "hidden"
  }

  // The keyboard key that brings it back, where the hide key was.
  Rectangle {
    id: showKey
    objectName: "lockKeyboardButton"
    readonly property real size: 56
    visible: root.inputEnabled && root.keyboardKnown && !root.keyboardOpen && keyboard.reveal === 0
    width: size
    height: size
    radius: size / 2
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 32
    color: Commons.Color.lock.background
    border.width: 1
    border.color: Util.alpha(Commons.Color.foreground, 0.25)

    KeyIcon {
      anchors.centerIn: parent
      width: 28
      height: 28
      name: "keyboard"
      color: Commons.Color.lock.text
    }

    MouseArea {
      anchors.fill: parent
      onClicked: {
        root.keyboardChoice = "shown"
        root.wakeRequested()
      }
    }
  }
}
