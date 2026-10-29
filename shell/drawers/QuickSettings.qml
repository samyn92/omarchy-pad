import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The right drawer: brightness and volume, the switches you reach for on a
// tablet, and a way into Omarchy's full panels. Laid out like those panels
// (section headers over sliders, separators between sections) so it reads as
// one of them.
Column {
  id: root

  property QuickSettingsState settings: null
  property QtObject palette: null
  // Asked to get out of the way, for a full panel opening in its place.
  signal closeRequested()

  readonly property color foreground: palette ? palette.foreground : Commons.Color.popups.text
  readonly property color dim: Qt.darker(foreground, 1.4)

  spacing: Style.spacing.panelGap

  component SliderSection: Column {
    id: section

    property string title: ""
    property string glyph: ""
    property int percent: 0
    property bool available: true
    property int minimum: 0
    // Every step of the finger, and where it let go.
    signal chosen(real value)

    width: root.width
    spacing: Style.space(6)
    visible: available

    Item {
      width: parent.width
      implicitHeight: Math.max(header.implicitHeight, readout.implicitHeight)

      PanelSectionHeader {
        id: header
        text: section.title
        foreground: root.foreground
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
      }

      Text {
        id: readout
        textFormat: Text.PlainText
        text: Commons.I18n.formatNumber(Math.round(slider.dragging ? slider.liveValue : section.percent)) + "%"
        color: root.dim
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        font.bold: true
        anchors.right: parent.right
        anchors.rightMargin: Style.space(6)
        anchors.verticalCenter: parent.verticalCenter
      }
    }

    Row {
      width: parent.width
      spacing: Style.spacing.rowGap

      Text {
        id: icon
        textFormat: Text.PlainText
        text: section.glyph
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.iconLarge
        width: Style.space(22)
        horizontalAlignment: Text.AlignHCenter
        anchors.verticalCenter: parent.verticalCenter
      }

      PanelSlider {
        id: slider
        bar: root.palette
        width: parent.width - icon.width - parent.spacing - Style.space(6)
        // Taller than a mouse needs: the whole row takes the finger.
        height: Math.max(implicitHeight, Style.space(36))
        minimum: section.minimum
        maximum: 100
        step: 5
        integer: true
        value: section.percent
        onMoved: function(v) { section.chosen(v) }
        onReleased: function(v) { section.chosen(v) }
      }
    }
  }

  component Switch: Toggle {
    width: (root.width - switches.columnSpacing) / 2
    implicitHeight: Style.space(48)
    foreground: root.foreground
    titleSize: Style.font.body
  }

  SliderSection {
    id: brightness
    title: Commons.I18n.t("monitor.brightness")
    glyph: "󰃠"
    percent: root.settings ? root.settings.brightness : 0
    available: !root.settings || root.settings.brightnessAvailable
    minimum: 1
    onChosen: function(v) { root.settings.setBrightness(v) }
  }

  SliderSection {
    id: volume
    title: "VOLUME"
    glyph: !root.settings || root.settings.muted || root.settings.volume === 0 ? "󰝟" : "󰕾"
    percent: root.settings ? root.settings.volume : 0
    available: !root.settings || root.settings.volumeAvailable
    onChosen: function(v) { root.settings.setVolume(v) }
  }

  PanelSeparator {
    width: parent.width
    foreground: root.foreground
  }

  Grid {
    id: switches
    width: parent.width
    columns: 2
    columnSpacing: Style.spacing.rowGap
    rowSpacing: Style.spacing.rowGap

    Switch {
      label: "Wi-Fi"
      checked: !!root.settings && root.settings.wifi
      enabled: !!root.settings && root.settings.wifiAvailable
      opacity: enabled ? 1 : 0.5
      onClicked: root.settings.setWifi(!checked)
    }
    Switch {
      label: Commons.I18n.t("bluetooth.title")
      checked: !!root.settings && root.settings.bluetooth
      onClicked: root.settings.setBluetooth(!checked)
    }
    Switch {
      label: "Do Not Disturb"
      checked: !!root.settings && root.settings.dnd
      onClicked: root.settings.setDnd(!checked)
    }
    Switch {
      label: Commons.I18n.t("bar.nightLight")
      checked: !!root.settings && root.settings.nightLight
      onClicked: root.settings.setNightLight(!checked)
    }
    Switch {
      label: Commons.I18n.t("bar.stayAwake")
      checked: !!root.settings && root.settings.stayAwake
      onClicked: root.settings.setStayAwake(!checked)
    }
    Switch {
      label: "Rotation Lock"
      checked: !!root.settings && root.settings.rotationLocked
      onClicked: root.settings.setRotationLocked(!checked)
    }
    Switch {
      label: "Keyboard"
      checked: !!root.settings && root.settings.keyboardShown
      enabled: !!root.settings && root.settings.keyboardAvailable
      opacity: enabled ? 1 : 0.5
      onClicked: root.settings.setKeyboardShown(!checked)
    }
  }

  PanelSeparator {
    width: parent.width
    foreground: root.foreground
  }

  // Omarchy's full panels and pickers, for what doesn't fit here: picking a
  // network, an output, a device, the display's scale, a theme or a
  // background.
  Grid {
    id: panels
    width: parent.width
    columns: 3
    columnSpacing: Style.spacing.rowGap
    rowSpacing: Style.spacing.rowGap

    Repeater {
      model: [
        { id: "omarchy.network", glyph: "󰖩", label: "Wi-Fi" },
        { id: "omarchy.audio", glyph: "󰕾", label: Commons.I18n.t("audio.title") },
        { id: "omarchy.bluetooth", glyph: "󰂯", label: Commons.I18n.t("bluetooth.title") },
        { id: "omarchy.monitor", glyph: "󰍹", label: "Display" },
        { id: "themes", glyph: "󰏘", label: "Theme" },
        { id: "backgrounds", glyph: "󰋩", label: "Background" }
      ]

      Button {
        required property var modelData
        width: (panels.width - panels.columnSpacing * 2) / 3
        iconText: modelData.glyph
        text: modelData.label
        bordered: true
        foreground: root.foreground
        onClicked: {
          root.closeRequested()
          if (modelData.id === "themes") root.settings.openThemes()
          else if (modelData.id === "backgrounds") root.settings.openBackgrounds()
          else root.settings.openPanel(modelData.id)
        }
      }
    }
  }
}
