import Quickshell
import Quickshell.Hyprland
import Quickshell.Io
import Quickshell.Wayland
import QtQuick
import qs.Commons
import qs.Commons as Commons

// Omarchy Pad's bar: a status bar for a tablet, as an Omarchy bar option. The
// time and date sit on the left, the workspaces in the middle and the status
// icons on the right. It stays at the top, and nothing on it can be dragged
// around or off: a finger pulled down from it opens a drawer instead.
//
// The widgets are Omarchy's own (the clock, battery, network...), from the
// shell's widget registry, so they look and behave as on Omarchy's bar and
// come along with Omarchy's updates. The bar gives them the part of Omarchy's
// bar they use: its colors and font, run, popouts and click targets.
//
// Pulling down from the left third opens the notifications drawer, from the
// right third quick settings (the omarchy-pad.drawers plugin, called in
// process), and from the middle the scratchpad console.
Item {
  id: root

  // Injected by the host shell, as into any bar option.
  property string omarchyPath: Quickshell.env("OMARCHY_PATH")
  property var barWidgetRegistry: null
  property var pluginRegistry: null
  property var barConfig: ({})
  property var shell: null
  property var manifest: null

  // What the bar carries, in order. A widget's settings are its defaults
  // here, then the user's own from their Omarchy bar layout, then its pinned
  // ones here: the clock shows the time and the date whatever format Omarchy's
  // bar gives it.
  readonly property var leftWidgets: [
    { id: "omarchy.clock", pinned: { format: "HH:mm  ddd d MMM" } }
  ]
  readonly property var centerWidgets: [
    { id: "omarchy.workspaces" }
  ]
  readonly property var rightWidgets: [
    { id: "omarchy.indicators" },
    { id: "omarchy.network" },
    { id: "omarchy.bluetooth" },
    { id: "omarchy.audio" },
    { id: "omarchy.monitor" },
    { id: "omarchy.power", defaults: { showPercentage: true } }
  ]

  // A pull opens its drawer when let go past this share of the drawer's
  // height, or when still moving down this fast, in px/s.
  readonly property real openShare: 0.35
  readonly property real openSpeed: 700

  // ---- What Omarchy's widgets read off their bar.

  readonly property string position: "top"
  readonly property bool vertical: false
  readonly property int barSize: Style.bar.sizeHorizontal
  property string fontFamily: Style.font.family
  property color foreground: Commons.Color.bar.text
  property color barForeground: foreground
  property color urgent: Commons.Color.bar.active
  // Omarchy's bar backgrounds: opaque, glassy (Hyprland blurs behind the
  // omarchy-bar namespace when Omarchy's bar-glassy toggle is on) or none.
  readonly property string look: {
    var config = barConfig || {}
    return String(config.background || (config.transparent === true ? "none" : "opaque"))
  }
  readonly property bool transparent: look === "none"
  property color themeBackground: Commons.Color.bar.background
  property color background: look === "none" ? "transparent"
    : look === "glassy" ? Qt.rgba(themeBackground.r, themeBackground.g, themeBackground.b, Math.min(themeBackground.a, 0.35))
    : themeBackground
  property bool foregroundAnimationEnabled: true
  Behavior on foreground { ColorAnimation { duration: Style.duration(250); easing.type: Easing.OutCubic } }
  Behavior on themeBackground { ColorAnimation { duration: Style.duration(250); easing.type: Easing.OutCubic } }

  // Hidden by omarchy-toggle-bar, as Omarchy's bar is.
  property bool barHidden: false
  property var activePopout: null
  property var popoutCard: null
  property var clickTargets: []
  readonly property var layoutConfig: ({ left: leftWidgets, centerBegin: [], center: centerWidgets, centerEnd: [], right: rightWidgets })
  // The indicators reveal their inactive icons while the pointer is on the
  // bar; a tablet has no pointer resting there, so only active ones show.
  readonly property bool centerSectionRevealHeld: false
  property bool centerHoverRevealSuppressed: false
  readonly property bool traySectionDragActive: false
  readonly property bool traySectionPopoutOpen: false
  readonly property var screenBarSizes: {
    var sizes = ({})
    var screens = Quickshell.screens
    for (var i = 0; i < screens.length; i++) sizes[screens[i].name] = root.barSize
    return sizes
  }
  // Every widget on every screen's bar.
  property var widgetHosts: []

  function barSizeFor(screenName) {
    return root.barSize
  }

  function run(command) {
    if (command) Util.execDetached(command)
  }

  // Tooltips appear on hover, which a finger doesn't do.
  function showTooltip(target, text) {}
  function hideTooltip(target) {}

  function registerClickTarget(target) {
    if (!target || clickTargets.indexOf(target) !== -1) return
    clickTargets = clickTargets.concat([target])
  }

  function unregisterClickTarget(target) {
    clickTargets = clickTargets.filter(function(item) { return item !== target })
  }

  // One panel open at a time, as on Omarchy's bar.
  function requestPopout(owner) {
    if (activePopout === owner) return
    if (activePopout) {
      if ("closeForPopoutSwitch" in activePopout) activePopout.closeForPopoutSwitch()
      else if ("close" in activePopout) activePopout.close()
    }
    activePopout = owner
  }

  function releasePopout(owner) {
    if (activePopout === owner) activePopout = null
  }

  function closePopout() {
    if (activePopout && typeof activePopout.close === "function") activePopout.close()
  }

  // Panels open fresh rather than taking over the last one's card.
  function notePopoutCard(card) { popoutCard = card }
  function popoutCardFor(owner) { return null }

  function setCenterHoverRevealSuppressed(value) {
    centerHoverRevealSuppressed = !!value
  }

  function targetBelongsToWindow(target, window) {
    return !!target && !!window && !!target.QsWindow && target.QsWindow.window === window
  }

  function moduleWidgets(id) {
    return widgetHosts.filter(function(host) { return host.moduleName === String(id) && host.activeItem })
      .map(function(host) { return host.activeItem })
  }

  function panelHosts(section, window) {
    return widgetHosts.filter(function(host) {
      var item = host.activeItem
      return (!section || host.section === section) && (!window || host.window === window)
        && item && item.visible && typeof item.open === "function" && item.opened !== undefined
    })
  }

  // The next panel along from an open one, on the same bar (keyboard
  // switching between panels).
  function switchPanelFrom(owner, direction) {
    var current = widgetHosts.filter(function(host) { return host.activeItem === owner })[0]
    if (!current) return false
    var hosts = panelHosts("", current.window)
    var index = hosts.indexOf(current)
    if (index < 0 || hosts.length < 2) return false
    hosts[(index + (direction < 0 ? -1 : 1) + hosts.length) % hosts.length].activeItem.open()
    return true
  }

  // ---- What the host shell calls on its bar.

  // The widget with a panel, on the screen Hyprland has focused if it is there.
  function findPanelWidget(id) {
    var hosts = panelHosts("").filter(function(host) { return host.moduleName === String(id) })
    var focused = Hyprland.focusedMonitor ? String(Hyprland.focusedMonitor.name || "") : ""
    var onFocused = hosts.filter(function(host) { return host.screenName === focused })
    var open = hosts.filter(function(host) { return host.activeItem.opened === true })
    var host = open[0] || onFocused[0] || hosts[0]
    return host ? host.activeItem : null
  }

  function summonBarWidget(id) {
    var item = findPanelWidget(id)
    if (!item) return false
    item.open()
    return true
  }

  function hideBarWidget(id) {
    var item = findPanelWidget(id)
    if (!item) return false
    item.close()
    return true
  }

  function isBarWidgetOpen(id) {
    var item = findPanelWidget(id)
    return !!item && item.opened === true
  }

  // One-based, for the panel hotkeys.
  function panelWidgetIdAt(region, index) {
    var screens = Quickshell.screens
    var hosts = panelHosts(String(region || ""), null).filter(function(host) {
      return screens.length === 0 || host.screenName === screens[0].name
    })
    var host = hosts[Math.round(Number(index)) - 1]
    return host ? host.moduleName : ""
  }

  function cycleBackground() {
    run("omarchy-bar background cycle")
  }

  function debugBarGeometry() {
    return widgetHosts.filter(function(host) { return host.activeItem }).map(function(host) {
      var point = host.mapToItem(null, 0, 0)
      return { id: host.moduleName, section: host.section, screen: host.screenName,
               x: point.x, y: point.y, width: host.width, height: host.height }
    })
  }

  // ---- Widgets.

  function entrySettings(entry) {
    var settings = Object.assign({}, entry.defaults || {})
    var layout = barConfig && barConfig.layout ? barConfig.layout : {}
    for (var section in layout) {
      var list = Array.isArray(layout[section]) ? layout[section] : []
      for (var i = 0; i < list.length; i++) {
        var own = list[i]
        if (own && typeof own === "object" && own.id === entry.id) {
          for (var key in own) if (key !== "id") settings[key] = own[key]
        }
      }
    }
    return Object.assign(settings, entry.pinned || {})
  }

  function registerHost(host) {
    if (widgetHosts.indexOf(host) === -1) widgetHosts = widgetHosts.concat([host])
  }

  function unregisterHost(host) {
    widgetHosts = widgetHosts.filter(function(item) { return item !== host })
  }

  component WidgetHost: Item {
    id: host

    required property var entry
    required property string section
    property var window: null
    readonly property string screenName: window && window.screen ? String(window.screen.name || "") : ""
    readonly property string moduleName: String(entry.id || "")
    readonly property var settings: root.entrySettings(entry)
    readonly property var component: {
      var widgets = root.barWidgetRegistry ? root.barWidgetRegistry.widgets : null
      return widgets && widgets[moduleName] ? widgets[moduleName].component : null
    }
    readonly property Item activeItem: loader.item

    // A widget with nothing to show (bluetooth with no adapter) hides itself
    // and takes no room.
    implicitWidth: activeItem && activeItem.visible ? activeItem.implicitWidth : 0
    implicitHeight: root.barSize

    function inject() {
      var target = activeItem
      if (!target) return
      if ("fixedEdge" in target) target.fixedEdge = section === "left" ? "start" : section === "right" ? "end" : "center"
      if ("bar" in target) target.bar = root
      if ("moduleName" in target) target.moduleName = moduleName
      if ("settings" in target) target.settings = settings
    }

    onSettingsChanged: inject()
    Component.onCompleted: root.registerHost(host)
    Component.onDestruction: root.unregisterHost(host)

    Loader {
      id: loader
      anchors.verticalCenter: parent.verticalCenter
      width: item ? item.implicitWidth : 0
      height: item ? item.implicitHeight : 0
      sourceComponent: host.component
      onLoaded: {
        host.inject()
        Qt.callLater(host.inject)
      }
    }
  }

  // ---- Pulling down.

  // The pull in progress: "left", "right", "middle", or "" for none.
  property string pullZone: ""
  // How far down the drawer opens, for the release rule.
  property real pullHeight: 0
  property real pullDistance: 0
  property real pullSpeed: 0
  property real pullMovedAt: 0
  property bool pullLifted: false
  property var pullScreen: null

  function drawers(method, args) {
    return IpcRegistry.call("omarchy-pad.drawers", method, args || [])
  }

  function beginPull(screen, share) {
    pullScreen = screen
    pullDistance = 0
    pullSpeed = 0
    pullLifted = false
    var zone = share < 1 / 3 ? "left" : share > 2 / 3 ? "right" : "middle"
    if (zone === "middle") {
      pullHeight = screen ? screen.height * 0.5 : 0
      consoleBegin(screen, share)
    } else {
      // The drawers decide which screen they open on; without them a pull
      // here does nothing.
      var begun = drawers("begin", [zone])
      if (!begun.ran) zone = ""
      pullHeight = begun.ran ? Number(begun.output) || 0 : 0
    }
    pullZone = zone
  }

  function followPull(distance, speed) {
    if (!pullZone) return
    pullDistance = Math.max(0, distance)
    pullSpeed = speed
    pullMovedAt = Date.now()
    if (pullZone === "middle") consoleFollow(pullDistance)
    else drawers("follow", [String(Math.round(pullDistance))])
  }

  // Let go, or taken away (a second finger: stage's gestures take over), in
  // which case it goes back up.
  function endPull() {
    if (!pullZone) return
    // A finger that stopped before it lifted is no longer moving.
    var speed = Date.now() - pullMovedAt > 100 ? 0 : pullSpeed
    var open = pullLifted && speed > -openSpeed
      && (pullDistance > pullHeight * openShare || speed > openSpeed)
    // An open panel goes once something comes down over it. Not sooner:
    // closing its popup while the finger is down ends the touch.
    if (open) closePopout()
    if (pullZone === "middle") consoleEnd(open)
    else drawers("settle", [open ? "1" : "0"])
    pullZone = ""
    pullScreen = null
  }

  // The scratchpad console. stage draws it coming down with the finger: the
  // bar hands it the pull over stage's command socket (`stage top begin`,
  // `follow`, `end`), the same one `stage simulate` uses. Without stage, a
  // pull past the threshold runs Omarchy's own toggle.
  Socket {
    id: stage
    path: Quickshell.env("XDG_RUNTIME_DIR") + "/stage-" + Quickshell.env("WAYLAND_DISPLAY") + ".sock"
    connected: true
    // stage answers each line with "ok"; the bar has nothing to do with it.
    parser: SplitParser { onRead: function(data) {} }
    // stage restarts with an install: the bar finds it again.
    onConnectedChanged: if (!connected) reconnect.restart()
  }

  Timer {
    id: reconnect
    interval: 2000
    onTriggered: stage.connected = true
  }

  property bool consoleToStage: false

  function tellStage(line) {
    stage.write(line + "\n")
    stage.flush()
  }

  function consoleBegin(screen, share) {
    consoleToStage = stage.connected
    if (!consoleToStage) return
    var x = screen ? share * screen.width : 0
    tellStage("top begin " + Math.round(x) + " " + Math.round(root.barSize / 2))
  }

  function consoleFollow(distance) {
    if (consoleToStage) tellStage("top follow " + Math.round(distance))
  }

  function consoleEnd(open) {
    if (consoleToStage) tellStage("top end " + (open ? "1" : "0"))
    else if (open) Hyprland.dispatch('hl.dsp.workspace.toggle_special("scratchpad")')
    consoleToStage = false
  }

  // ---- See-through on a tap.

  // A tap on the bar's empty space makes it clear, and the next one brings
  // back the look it had, through Omarchy's own omarchy-bar background, so
  // shell.json and Hyprland's blur stay in step as with Omarchy's bar. The look
  // to come back to is kept in a file, as a restart clears everything else.
  readonly property string lookFile: (Quickshell.env("XDG_STATE_HOME") || Quickshell.env("HOME") + "/.local/state")
    + "/omarchy-pad/bar-look"

  FileView {
    id: lookMemory
    path: root.lookFile
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
  }

  function toggleTransparency() {
    if (look === "none") {
      var back = String(lookMemory.text() || "").trim()
      run("omarchy-bar background " + (back === "glassy" ? "glassy" : "opaque"))
      return
    }
    var dir = root.lookFile.replace(/\/[^\/]*$/, "")
    run("mkdir -p " + Util.shellQuote(dir) + " && printf %s " + Util.shellQuote(look)
      + " > " + Util.shellQuote(root.lookFile) + " && omarchy-bar background none")
  }

  // ---- Hidden by omarchy-toggle-bar.

  // The flag is a file; the toggles directory is watched because a file that
  // doesn't exist yet can't be.
  Process {
    id: barHiddenProbe
    running: true
    command: ["bash", "-c", "[[ -f $HOME/.local/state/omarchy/toggles/bar-off ]] && echo yes || echo no"]
    stdout: SplitParser { onRead: function(line) { root.barHidden = String(line).trim() === "yes" } }
  }
  FileView {
    path: Quickshell.env("HOME") + "/.local/state/omarchy/toggles"
    watchChanges: true
    printErrors: false
    onFileChanged: barHiddenProbe.running = true
  }
  // omarchy-toggle-bar also nudges Omarchy's bar over IPC, as "omarchy.bar";
  // that target stays Omarchy's, so this bar goes by the watch alone.

  Variants {
    model: Quickshell.screens

    delegate: Component {
      PanelWindow {
        id: barWindow

        required property var modelData
        readonly property bool pulling: root.pullZone !== "" && root.pullScreen === modelData

        screen: modelData
        // The surface spans the screen and takes input on the strip alone,
        // as Omarchy's bar does. While a pull is on, all of it takes input,
        // so a mouse pulling past the strip stays with the bar.
        anchors { top: true; left: true; right: true }
        implicitHeight: screen ? screen.height : root.barSize
        exclusionMode: root.barHidden ? ExclusionMode.Ignore : ExclusionMode.Normal
        exclusiveZone: root.barSize
        margins.top: root.barHidden ? -implicitHeight : 0
        mask: pulling ? null : stripRegion
        color: "transparent"
        surfaceFormat.opaque: false
        WlrLayershell.namespace: "omarchy-bar"
        WlrLayershell.layer: WlrLayer.Top

        Region { id: stripRegion; item: strip }

        Rectangle {
          id: strip

          anchors { top: parent.top; left: parent.left; right: parent.right }
          height: root.barSize
          color: root.background

          // Under the widgets, so only a tap on empty bar space lands here.
          MouseArea {
            anchors.fill: parent
            onClicked: root.toggleTransparency()
          }

          Row {
            anchors { left: parent.left; leftMargin: Style.space(10); verticalCenter: parent.verticalCenter }
            Repeater {
              model: root.leftWidgets
              delegate: WidgetHost {
                required property var modelData
                entry: modelData
                section: "left"
                window: barWindow
              }
            }
          }

          Row {
            anchors.centerIn: parent
            Repeater {
              model: root.centerWidgets
              delegate: WidgetHost {
                required property var modelData
                entry: modelData
                section: "center"
                window: barWindow
              }
            }
          }

          Row {
            anchors { right: parent.right; rightMargin: Style.space(6); verticalCenter: parent.verticalCenter }
            Repeater {
              model: root.rightWidgets
              delegate: WidgetHost {
                required property var modelData
                entry: modelData
                section: "right"
                window: barWindow
              }
            }
          }

          // Under the widgets, so a press reaches the widget first and taps
          // are the widgets' own; Qt still shows it to this handler, which
          // only watches. Once the finger has gone a few pixels down, the
          // pull takes the press from the widget, which then doesn't count it
          // as a tap. (Laid over the widgets instead, it would swallow mouse
          // presses.) One finger only; a second ends the pull.
          DragHandler {
            id: pull

            target: null
            xAxis.enabled: false
            maximumPointCount: 1

            function distance() {
              return centroid.scenePosition.y - centroid.scenePressPosition.y
            }

            onActiveChanged: {
              if (active) {
                root.beginPull(barWindow.screen, centroid.scenePressPosition.x / Math.max(1, strip.width))
                root.followPull(distance(), centroid.velocity.y)
              } else {
                // Whether the finger lifted comes with the grab change that
                // follows.
                Qt.callLater(root.endPull)
              }
            }
            onCentroidChanged: if (active) root.followPull(distance(), centroid.velocity.y)
            onGrabChanged: function(transition, point) {
              if (barWindow.pulling && point.state === EventPoint.Released) root.pullLifted = true
            }
          }
        }
      }
    }
  }
}
