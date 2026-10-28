import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The left drawer: what is on screen and what came in recently, newest first.
// Each card is Omarchy's own toast, loaded from its notifications plugin, so
// it is drawn exactly as it was when it arrived. The cards only show: the
// service keeps no way to act on a closed notification, and none to remove
// one from its history.
Column {
  id: root

  property NotificationFeed feed: null
  property QuickSettingsState settings: null
  property QtObject palette: null
  // The most the drawer may take; the list scrolls within it.
  property real maxHeight: 600

  readonly property color foreground: palette ? palette.foreground : Commons.Color.popups.text
  readonly property int liveCount: feed ? feed.live.length : 0
  readonly property int recentCount: feed ? feed.recent.length : 0

  // Whether this drawer is the one on screen.
  property bool shown: false

  // Toasts write "5 min ago", "14:02", "yesterday" against this.
  property double now: Date.now()
  function tick() { root.now = Date.now() }
  onShownChanged: if (shown) tick()
  Timer {
    interval: 30000
    repeat: true
    running: root.shown
    onTriggered: root.tick()
  }

  spacing: Style.spacing.panelGap

  Item {
    id: header
    width: parent.width
    implicitHeight: Math.max(title.implicitHeight, quiet.implicitHeight, dismissAll.implicitHeight)

    Text {
      id: title
      textFormat: Text.PlainText
      text: "Notifications"
      color: root.foreground
      font.family: Style.font.family
      font.pixelSize: Style.font.title
      font.bold: true
      anchors.left: parent.left
      anchors.verticalCenter: parent.verticalCenter
    }

    Row {
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      spacing: Style.spacing.rowGap

      Button {
        id: dismissAll
        visible: root.liveCount > 0
        text: "Dismiss all"
        bordered: true
        foreground: root.foreground
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.settings.dismissAllNotifications()
      }

      Text {
        textFormat: Text.PlainText
        text: "Do Not Disturb"
        color: Qt.darker(root.foreground, 1.4)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        font.bold: true
        anchors.verticalCenter: parent.verticalCenter
      }

      ToggleSwitch {
        id: quiet
        checked: !!root.settings && root.settings.dnd
        foreground: root.foreground
        anchors.verticalCenter: parent.verticalCenter
        onToggled: root.settings.setDnd(!checked)
      }
    }
  }

  PanelSeparator {
    width: parent.width
    foreground: root.foreground
  }

  Flickable {
    id: scroller
    width: parent.width
    height: Math.min(contentHeight, Math.max(Style.space(80), root.maxHeight - header.height - root.spacing * 2 - Style.space(1)))
    contentHeight: list.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentHeight > height

    Column {
      id: list
      width: scroller.width
      spacing: Style.spacing.rowGap

      PanelSectionHeader {
        visible: root.liveCount > 0
        text: "ON SCREEN"
        foreground: root.foreground
      }

      Repeater {
        model: root.feed ? root.feed.live : []
        delegate: toastSlot
      }

      PanelSectionHeader {
        visible: root.recentCount > 0
        text: "EARLIER"
        foreground: root.foreground
      }

      Repeater {
        model: root.feed ? root.feed.recent : []
        delegate: toastSlot
      }

      Text {
        visible: root.liveCount === 0 && root.recentCount === 0
        textFormat: Text.PlainText
        text: Commons.I18n.t("notifications.noneRecent")
        color: Qt.darker(root.foreground, 1.4)
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        topPadding: Style.space(12)
        bottomPadding: Style.space(12)
      }
    }
  }

  Component {
    id: toastSlot

    Item {
      id: slot
      required property var modelData
      width: list.width
      height: toast.item ? toast.item.height : 0

      // Open as in Omarchy's expanded deck: the whole body, no stacking.
      Loader {
        id: toast
        Component.onCompleted: {
          if (root.feed && root.feed.toastUrl)
            setSource(root.feed.toastUrl, { row: slot.modelData, expanded: true, cardWidth: slot.width, now: root.now })
        }
      }

      Binding {
        target: toast.item
        property: "cardWidth"
        value: slot.width
        when: !!toast.item
      }
      Binding {
        target: toast.item
        property: "now"
        value: root.now
        when: !!toast.item
      }
    }
  }
}
