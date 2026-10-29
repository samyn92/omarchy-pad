import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Networking
import Quickshell.Services.UPower

// What the quick settings drawer shows, and the commands it runs. Each switch
// goes through the command Omarchy's own panels and keys use, so the drawer
// and the rest of the desktop never disagree about how a thing is turned on.
// The state is read once per pull (refresh()), not polled: the drawer is only
// on screen for a moment.
Scope {
  id: root

  property string pluginDir: ""

  // Percentages, 0 while unknown. A slider is hidden only once there turns
  // out to be nothing to control, so the drawer keeps its height while the
  // first read is still out.
  property int brightness: 0
  property int volume: 0
  property bool brightnessAvailable: true
  property bool volumeAvailable: true
  property bool muted: false
  property bool bluetooth: false
  property bool dnd: false
  property bool nightLight: false
  property bool stayAwake: false
  property bool rotationLocked: false
  property bool keyboardShown: false
  property bool keyboardAvailable: false
  // The Wi-Fi network and the Bluetooth device in use, "" for none.
  property string wifiName: ""
  property string bluetoothDevice: ""
  // Omarchy's power profile (power-saver, balanced, performance), "" where
  // there are none to pick from.
  property string powerProfile: ""

  // The battery, as Omarchy's power panel reads it.
  readonly property var batteryDevice: UPower.displayDevice
  readonly property bool batteryAvailable: !!batteryDevice && batteryDevice.isPresent
  readonly property int batteryPercent: batteryAvailable ? Math.round(batteryDevice.percentage * 100) : 0
  readonly property bool charging: batteryAvailable && (batteryDevice.state === UPowerDeviceState.Charging
                                                        || batteryDevice.state === UPowerDeviceState.FullyCharged)

  // Wi-Fi is a NetworkManager property the shell already holds, as in
  // Omarchy's network panel; there is no command to wait on.
  readonly property bool wifiAvailable: Networking.backend === NetworkBackendType.NetworkManager
  readonly property bool wifi: Networking.wifiEnabled

  // A switch whose command is still running keeps the value it was thrown to,
  // so a state read that started before it can't throw it back.
  property var busy: ({})

  signal commandRan(var argv)

  Component.onCompleted: refresh()

  function refresh() {
    if (stateProc.running) {
      stateProc.queued = true
      return
    }
    stateProc.running = true
  }

  function apply(line) {
    var at = String(line).indexOf("=")
    if (at < 0) return
    var key = line.substring(0, at)
    var value = line.substring(at + 1).trim()
    if (root.busy[key]) return
    var on = value === "on" || value === "true" || value === "shown"
    var number = /^[0-9]+$/.test(value) ? Math.max(0, Math.min(100, parseInt(value, 10))) : -1
    if (key === "brightness") {
      root.brightnessAvailable = number >= 0
      if (number >= 0) root.brightness = number
    } else if (key === "volume") {
      root.volumeAvailable = number >= 0
      if (number >= 0) root.volume = number
    }
    else if (key === "muted") root.muted = on
    else if (key === "wifiname") root.wifiName = value
    else if (key === "bluetoothdevice") root.bluetoothDevice = value
    else if (key === "profile") root.powerProfile = value === "unavailable" ? "" : value
    else if (key === "bluetooth") root.bluetooth = on
    else if (key === "dnd") root.dnd = on
    else if (key === "nightlight") root.nightLight = on
    else if (key === "stayawake") root.stayAwake = on
    else if (key === "rotationlock") root.rotationLocked = on
    else if (key === "keyboard") {
      root.keyboardAvailable = value === "shown" || value === "hidden"
      root.keyboardShown = on
    }
  }

  function markBusy(key, value) {
    var next = ({})
    for (var k in root.busy) next[k] = root.busy[k]
    if (value) next[key] = true
    else delete next[key]
    root.busy = next
  }

  // Runs one command for the switch called `key`, then reads the state back.
  function run(key, argv) {
    root.commandRan(argv)
    if (key) root.markBusy(key, true)
    var proc = commandComponent.createObject(root, { command: argv, key: key })
    proc.running = true
  }

  Component {
    id: commandComponent

    Process {
      property string key: ""
      onExited: {
        if (key) root.markBusy(key, false)
        if (key) root.refresh()
        destroy()
      }
    }
  }

  function setBluetooth(on) {
    root.bluetooth = on
    root.run("bluetooth", ["omarchy-bluetooth-power", on ? "on" : "off"])
  }

  function setDnd(on) {
    root.dnd = on
    root.run("dnd", ["omarchy-shell", "notifications", "setDnd", on ? "on" : "off"])
  }

  function setNightLight(on) {
    root.nightLight = on
    root.run("nightlight", ["omarchy-shell", "nightlight", on ? "enable" : "disable"])
  }

  // Stay awake is the idle service turned off.
  function setStayAwake(on) {
    root.stayAwake = on
    root.run("stayawake", ["omarchy-shell", "idle", on ? "disable" : "enable"])
  }

  function setRotationLocked(on) {
    root.rotationLocked = on
    root.run("rotationlock", ["iceland-rotate-lock", on ? "on" : "off"])
  }

  function setKeyboardShown(on) {
    root.keyboardShown = on
    root.run("keyboard", ["floatkey", on ? "show" : "hide"])
  }

  function setWifi(on) {
    if (root.wifiAvailable) Networking.wifiEnabled = on
  }

  // Opens one of Omarchy's own panels (the Wi-Fi list, audio devices...). A
  // plugin may only summon itself, so this goes through omarchy-shell like a
  // keybinding would.
  function openPanel(id) {
    root.run("", ["omarchy-shell", "shell", "summon", id, ""])
  }

  // Omarchy's theme and background pickers, as its menu opens them (with
  // Omarchy Pad's picker installed, a finger can swipe through them).
  function openThemes() {
    root.run("", ["omarchy-shell", "shell", "summon", "omarchy.image-picker", '{"source":"themes"}'])
  }

  function openBackgrounds() {
    root.run("", ["omarchy-theme-bg-switcher"])
  }

  // For whichever of AC and battery the tablet is on now, as Omarchy's power
  // panel does.
  function setPowerProfile(profile) {
    root.powerProfile = profile
    root.run("profile", ["omarchy-powerprofiles-set", "autodetect", profile])
  }

  // Omarchy's screenshot (fullscreen, region), screen text and QR code
  // capture. They start once the drawer has gone up, so it isn't in the
  // picture and a region is picked over what is really there.
  function capture(kind) {
    var argv = kind === "text" ? ["omarchy-capture-text"]
      : kind === "qr" ? ["omarchy-capture-qr"]
      : ["omarchy-capture-screenshot", kind === "region" ? "region" : "fullscreen"]
    root.run("", ["bash", "-c", 'sleep 0.5; exec "$@"', "capture"].concat(argv))
  }

  function openClipboard() {
    root.run("", ["omarchy-shell", "shell", "summon", "omarchy.clipboard", ""])
  }

  function dismissAllNotifications() {
    root.run("", ["omarchy-shell", "notifications", "dismissAll"])
  }

  // Omarchy dismisses toasts by their summary; the one swiped away is the one
  // with it (and any other on screen saying the same).
  function dismissNotification(summary) {
    if (summary) root.run("", ["omarchy-shell", "notifications", "dismiss", summary])
  }

  // Sliders send a value per step of the finger. One command runs at a time
  // and the latest value waits for it, as in Omarchy's monitor panel, so a
  // quick drag doesn't queue up a backlog that keeps going after the finger
  // stops.
  function setBrightness(percent) {
    root.brightness = Math.max(1, Math.min(100, Math.round(percent)))
    brightnessProc.send(root.brightness)
  }

  function setVolume(percent) {
    root.volume = Math.max(0, Math.min(100, Math.round(percent)))
    if (root.volume > 0) root.muted = false
    volumeProc.send(root.volume)
  }

  // Without the OSD: the drawer is the display.
  Process {
    id: brightnessProc
    property int queued: -1
    function send(percent) {
      if (running) { queued = percent; return }
      command = ["omarchy-brightness-display", "--no-osd", percent + "%"]
      running = true
    }
    onExited: if (queued >= 0) { var next = queued; queued = -1; send(next) }
  }

  // omarchy-audio-output-volume moves the volume in steps and shows the OSD.
  // This sets it outright on the same sink that script resolves (through any
  // speaker DSP to the physical output), and unmutes it the same way.
  Process {
    id: volumeProc
    property int queued: -1
    function send(percent) {
      if (running) { queued = percent; return }
      command = ["bash", "-c",
        'sink=$(omarchy-audio-output-sink) && [[ -n $sink ]] || exit 1; '
        + 'pactl set-sink-mute "$sink" 0; pactl set-sink-volume "$sink" "$1%"',
        "volume", String(percent)]
      running = true
    }
    onExited: if (queued >= 0) { var next = queued; queued = -1; send(next) }
  }

  Process {
    id: stateProc
    property bool queued: false
    command: ["bash", root.pluginDir + "/quick-settings-state"]
    stdout: SplitParser {
      onRead: function(line) { root.apply(line) }
    }
    onExited: if (queued) { queued = false; running = true }
  }
}
