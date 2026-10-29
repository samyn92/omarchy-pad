import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The right drawer's first page, laid out for a finger: everything once,
// nothing behind a second button that a first one already shows.
//
//  - Wi-Fi and Bluetooth, each a wide tile saying what it is on: a tap turns
//    it on or off, its arrow opens Omarchy's panel (networks, devices).
//  - Brightness and volume, as bars as tall as a fingertip: a finger moves
//    them along from wherever it lands, so a swipe that starts on one to turn
//    the page doesn't change it. Beside each, Omarchy's panel for it
//    (display, sound output).
//  - Switches and actions in a grid of four: Focus (Do Not Disturb), Night
//    Light, Stay Awake, rotation lock; screenshots of the screen or an area,
//    the on-screen keyboard, the clipboard history; Omarchy's theme and
//    background pickers, and text and QR codes read off the screen.
//  - The battery, and Omarchy's power profile.
//
// Built from Omarchy's own pieces (its surfaces, borders, fills, glyphs and
// segmented buttons), so it follows the theme as its panels do.
Column {
  id: root

  property QuickSettingsState settings: null
  property QtObject palette: null
  // Asked to get out of the way, for a full panel opening in its place or a
  // capture of the screen.
  signal closeRequested()

  readonly property color foreground: palette ? palette.foreground : Commons.Color.popups.text
  readonly property color accent: Commons.Color.accent
  readonly property color dim: Qt.darker(foreground, 1.4)
  readonly property real gap: Style.spacing.rowGap

  spacing: Style.spacing.panelGap

  // What a control looks like, pressed or on, as Omarchy's buttons do.
  function fillFor(pressed, on) {
    return pressed ? Style.pressedFillFor(root.foreground, root.accent)
      : on ? Style.selectedFillFor(root.foreground, root.accent)
      : "transparent"
  }
  function borderFor(on) {
    return Border.controlSpec(on && Border.controlHasWidth("selected") ? "selected" : "normal", root.foreground, root.accent)
  }

  // Shut the drawer, then do it: a panel opening, or a capture.
  function away(action) {
    root.closeRequested()
    action()
  }

  // Wi-Fi or Bluetooth: on or off, and what it is on with.
  component LinkTile: BorderSurface {
    id: link

    property string glyph: ""
    property string offGlyph: glyph
    property string title: ""
    property string detail: ""
    property bool on: false
    property bool available: true
    signal toggled()
    signal opened()

    height: Style.space(56)
    radius: Style.cornerRadius
    color: root.fillFor(switchArea.pressed, link.on)
    borderSpec: root.borderFor(link.on)
    opacity: available ? 1 : 0.5

    Row {
      x: link.contentLeftInset + Style.space(10)
      anchors.verticalCenter: parent.verticalCenter
      width: arrow.x - x
      spacing: Style.space(10)

      Text {
        textFormat: Text.PlainText
        text: link.on ? link.glyph : link.offGlyph
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.iconLarge
        anchors.verticalCenter: parent.verticalCenter
      }

      Column {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - Style.space(40)

        Text {
          textFormat: Text.PlainText
          text: link.title
          color: root.foreground
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          font.bold: true
          elide: Text.ElideRight
          width: parent.width
        }
        Text {
          textFormat: Text.PlainText
          text: link.detail || (link.on ? "On" : "Off")
          color: root.dim
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
          elide: Text.ElideRight
          width: parent.width
        }
      }
    }

    MouseArea {
      id: switchArea
      anchors.left: parent.left
      anchors.right: arrow.left
      height: parent.height
      enabled: link.available
      onClicked: link.toggled()
    }

    // The way into Omarchy's panel, set off by a rule.
    Rectangle {
      x: arrow.x
      anchors.verticalCenter: parent.verticalCenter
      width: Math.max(1, Style.space(1))
      height: parent.height * 0.5
      color: root.dim
      opacity: 0.35
    }
    Item {
      id: arrow
      x: link.width - width - link.contentRightInset
      width: Style.space(40)
      height: parent.height

      Rectangle {
        anchors.fill: parent
        color: arrowArea.pressed ? Style.pressedFillFor(root.foreground, root.accent) : "transparent"
      }
      Text {
        anchors.centerIn: parent
        textFormat: Text.PlainText
        text: "\u{f0142}"
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.icon
      }
      MouseArea {
        id: arrowArea
        anchors.fill: parent
        onClicked: link.opened()
      }
    }
  }

  // A bar as tall as a fingertip, filled as far as the value. The finger
  // moves it by as much as it moves, from wherever it lands.
  component FatSlider: BorderSurface {
    id: slider

    property string glyph: ""
    property int percent: 0
    property int minimum: 0
    property bool available: true
    property real liveValue: percent
    readonly property bool dragging: drag.active
    // Every step of the finger.
    signal chosen(real value)

    height: Style.space(48)
    radius: Style.cornerRadius
    color: "transparent"
    borderSpec: root.borderFor(false)
    opacity: available ? 1 : 0.5
    clip: true

    readonly property real shown: dragging ? liveValue : percent

    Rectangle {
      x: slider.contentLeftInset
      y: slider.contentTopInset
      height: slider.height - slider.contentTopInset - slider.contentBottomInset
      width: (slider.width - slider.contentLeftInset - slider.contentRightInset) * Math.max(0, Math.min(1, slider.shown / 100))
      color: Style.selectedFillFor(root.foreground, root.accent)
    }

    Text {
      x: slider.contentLeftInset + Style.space(14)
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: slider.glyph
      color: root.foreground
      font.family: Style.font.family
      font.pixelSize: Style.font.iconLarge
    }

    Text {
      anchors.right: parent.right
      anchors.rightMargin: slider.contentRightInset + Style.space(14)
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: Commons.I18n.formatNumber(Math.round(slider.shown)) + "%"
      color: root.dim
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
      font.bold: true
    }

    DragHandler {
      id: drag
      target: null
      yAxis.enabled: false
      enabled: slider.available
      property real from: 0
      onActiveChanged: if (active) from = slider.percent
      onActiveTranslationChanged: {
        var span = Math.max(1, slider.width)
        slider.liveValue = Math.max(slider.minimum, Math.min(100, from + activeTranslation.x / span * 100))
        slider.chosen(slider.liveValue)
      }
    }
  }

  // A square of the grid: a switch (on or off) or an action, its name under it.
  component GridTile: Item {
    id: tile
    objectName: "tile-" + label

    property string glyph: ""
    property string label: ""
    property bool on: false
    property bool available: true
    signal tapped()

    width: (root.width - root.gap * 3) / 4
    height: box.height + Style.space(4) + name.implicitHeight
    opacity: available ? 1 : 0.5

    BorderSurface {
      id: box
      anchors.horizontalCenter: parent.horizontalCenter
      width: Math.min(tile.width, Style.space(64))
      height: width
      radius: Style.cornerRadius
      color: root.fillFor(area.pressed, tile.on)
      borderSpec: root.borderFor(tile.on)

      Text {
        anchors.centerIn: parent
        textFormat: Text.PlainText
        text: tile.glyph
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.iconLarge
      }
    }

    Text {
      id: name
      anchors.top: box.bottom
      anchors.topMargin: Style.space(4)
      width: tile.width
      horizontalAlignment: Text.AlignHCenter
      textFormat: Text.PlainText
      text: tile.label
      color: tile.on ? root.foreground : root.dim
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
      elide: Text.ElideRight
    }

    MouseArea {
      id: area
      anchors.fill: box
      enabled: tile.available
      onClicked: tile.tapped()
    }
  }

  // A slider and, beside it, the way into Omarchy's panel for it.
  component SliderRow: Row {
    id: sliderRow

    property alias glyph: bar.glyph
    property alias percent: bar.percent
    property alias minimum: bar.minimum
    property alias available: bar.available
    property string panelGlyph: ""
    signal chosen(real value)
    signal opened()

    width: root.width
    spacing: root.gap

    FatSlider {
      id: bar
      objectName: sliderRow.objectName + "-bar"
      width: sliderRow.width - side.width - sliderRow.spacing
      onChosen: function(v) { sliderRow.chosen(v) }
    }

    BorderSurface {
      id: side
      width: bar.height
      height: bar.height
      radius: Style.cornerRadius
      color: root.fillFor(sideArea.pressed, false)
      borderSpec: root.borderFor(false)

      Text {
        anchors.centerIn: parent
        textFormat: Text.PlainText
        text: sliderRow.panelGlyph
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.icon
      }
      MouseArea {
        id: sideArea
        anchors.fill: parent
        onClicked: sliderRow.opened()
      }
    }
  }

  Row {
    width: parent.width
    spacing: root.gap

    LinkTile {
      width: (root.width - root.gap) / 2
      glyph: "\u{f05a9}"
      offGlyph: "\u{f05aa}"
      title: "Wi-Fi"
      detail: !!root.settings && root.settings.wifi ? root.settings.wifiName : ""
      on: !!root.settings && root.settings.wifi
      available: !!root.settings && root.settings.wifiAvailable
      onToggled: root.settings.setWifi(!on)
      onOpened: root.away(function() { root.settings.openPanel("omarchy.network") })
    }

    LinkTile {
      width: (root.width - root.gap) / 2
      glyph: "\u{f00af}"
      offGlyph: "\u{f00b2}"
      title: Commons.I18n.t("bluetooth.title")
      detail: !!root.settings && root.settings.bluetooth ? root.settings.bluetoothDevice : ""
      on: !!root.settings && root.settings.bluetooth
      onToggled: root.settings.setBluetooth(!on)
      onOpened: root.away(function() { root.settings.openPanel("omarchy.bluetooth") })
    }
  }

  SliderRow {
    objectName: "brightness"
    glyph: "\u{f00e0}"
    panelGlyph: "\u{f0379}"
    percent: root.settings ? root.settings.brightness : 0
    minimum: 1
    available: !root.settings || root.settings.brightnessAvailable
    onChosen: function(v) { root.settings.setBrightness(v) }
    onOpened: root.away(function() { root.settings.openPanel("omarchy.monitor") })
  }

  SliderRow {
    glyph: !root.settings || root.settings.muted || root.settings.volume === 0 ? "\u{f075f}" : "\u{f057e}"
    panelGlyph: "\u{f02cb}"
    percent: root.settings ? root.settings.volume : 0
    available: !root.settings || root.settings.volumeAvailable
    onChosen: function(v) { root.settings.setVolume(v) }
    onOpened: root.away(function() { root.settings.openPanel("omarchy.audio") })
  }

  Grid {
    width: parent.width
    columns: 4
    columnSpacing: root.gap
    rowSpacing: root.gap

    GridTile {
      glyph: "\u{f009b}"
      label: "Focus"
      on: !!root.settings && root.settings.dnd
      onTapped: root.settings.setDnd(!on)
    }
    GridTile {
      glyph: "\u{f050e}"
      label: Commons.I18n.t("bar.nightLight")
      on: !!root.settings && root.settings.nightLight
      onTapped: root.settings.setNightLight(!on)
    }
    GridTile {
      glyph: "\u{ef59}"
      label: Commons.I18n.t("bar.stayAwake")
      on: !!root.settings && root.settings.stayAwake
      onTapped: root.settings.setStayAwake(!on)
    }
    GridTile {
      glyph: "\u{f0478}"
      label: "Rotation"
      on: !!root.settings && root.settings.rotationLocked
      onTapped: root.settings.setRotationLocked(!on)
    }
    GridTile {
      glyph: "\u{f0e51}"
      label: "Screenshot"
      onTapped: root.away(function() { root.settings.capture("fullscreen") })
    }
    GridTile {
      glyph: "\u{f019e}"
      label: "Region"
      onTapped: root.away(function() { root.settings.capture("region") })
    }
    GridTile {
      glyph: "\u{f030c}"
      label: "Keyboard"
      on: !!root.settings && root.settings.keyboardShown
      available: !!root.settings && root.settings.keyboardAvailable
      onTapped: root.settings.setKeyboardShown(!on)
    }
    GridTile {
      glyph: "\u{f014c}"
      label: "Clipboard"
      onTapped: root.away(function() { root.settings.openClipboard() })
    }
    GridTile {
      glyph: "\u{f03d8}"
      label: "Theme"
      onTapped: root.away(function() { root.settings.openThemes() })
    }
    GridTile {
      glyph: "\u{f02e9}"
      label: "Background"
      onTapped: root.away(function() { root.settings.openBackgrounds() })
    }
    GridTile {
      glyph: "\u{f113a}"
      label: "Text"
      onTapped: root.away(function() { root.settings.capture("text") })
    }
    GridTile {
      glyph: "\u{f0433}"
      label: "QR code"
      onTapped: root.away(function() { root.settings.capture("qr") })
    }
  }

  // The battery, and the power profile it runs on.
  Item {
    width: parent.width
    implicitHeight: Math.max(battery.implicitHeight, profiles.implicitHeight)
    visible: !!root.settings && (root.settings.batteryAvailable || root.settings.powerProfile !== "")

    Text {
      id: battery
      visible: !!root.settings && root.settings.batteryAvailable
      anchors.left: parent.left
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: (root.settings && root.settings.charging ? "\u{f0084}" : "\u{f0079}") + " "
        + Commons.I18n.formatNumber(root.settings ? root.settings.batteryPercent : 0) + "%"
      color: root.foreground
      font.family: Style.font.family
      font.pixelSize: Style.font.body
      font.bold: true
    }

    ButtonGroup {
      id: profiles
      objectName: "profiles"
      visible: !!root.settings && root.settings.powerProfile !== ""
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      focusable: false
      foreground: root.foreground
      options: [
        { value: "power-saver", label: "Saver" },
        { value: "balanced", label: "Balanced" },
        { value: "performance", label: "Fast" }
      ]
      value: root.settings ? root.settings.powerProfile : ""
      onChanged: function(v) { root.settings.setPowerProfile(v) }
    }
  }

  // Where the agents are: on past this page, as the drawer came.
  Text {
    width: parent.width
    horizontalAlignment: Text.AlignHCenter
    textFormat: Text.PlainText
    text: "\u{f0140} Pull on down for Agents"
    color: Qt.darker(root.foreground, 1.8)
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }
}
