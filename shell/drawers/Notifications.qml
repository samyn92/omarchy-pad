import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The left drawer: what is on screen and what came in recently, newest first.
// Each card is Omarchy's own toast, loaded from its notifications plugin, so
// it is drawn exactly as it was when it arrived, and the recent ones stack by
// app as Omarchy's toasts do. A finger swipes a card away (a stack goes
// whole) and taps a stack open. One on screen is dismissed in Omarchy too; a
// recent one only leaves the drawer, as Omarchy keeps no way to remove it
// from its history (see NotificationFeed).
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

  // The stacks tapped open, by their group key.
  property var openStacks: ({})
  function setStackOpen(key, open) {
    var next = Object.assign({}, root.openStacks)
    if (open) next[key] = true
    else delete next[key]
    root.openStacks = next
  }
  onShownChanged: {
    if (shown) tick()
    else root.openStacks = ({})
  }

  // Leaves these rows out, and dismisses any still on screen.
  function remove(rows, onScreen) {
    if (onScreen)
      for (var i = 0; i < rows.length; i++) root.settings.dismissNotification(rows[i].summary)
    root.feed.hide(rows)
  }

  // Toasts write "5 min ago", "14:02", "yesterday" against this.
  property double now: Date.now()
  function tick() { root.now = Date.now() }
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
        delegate: SwipeCard {
          required property var modelData
          rows: [modelData]
          onRemoved: root.remove(rows, true)
        }
      }

      Item {
        visible: root.recentCount > 0
        width: parent.width
        implicitHeight: Math.max(earlier.implicitHeight, clear.implicitHeight)

        PanelSectionHeader {
          id: earlier
          text: "EARLIER"
          foreground: root.foreground
          anchors.left: parent.left
          anchors.verticalCenter: parent.verticalCenter
        }

        Button {
          id: clear
          text: "Clear"
          bordered: true
          foreground: root.foreground
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          onClicked: root.feed.hide(root.feed.allRecent)
        }
      }

      Repeater {
        model: root.feed ? root.feed.recentGroups : []
        delegate: Column {
          id: group
          required property var modelData
          readonly property bool open: modelData.rows.length > 1 && root.openStacks[modelData.key] === true
          width: list.width
          spacing: Style.spacing.rowGap

          // An open stack says whose it is, and folds back up with a tap.
          Item {
            visible: group.open
            width: parent.width
            implicitHeight: less.implicitHeight

            Text {
              textFormat: Text.PlainText
              text: group.modelData.rows[0].app || ""
              color: root.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.body
              font.bold: true
              elide: Text.ElideRight
              width: parent.width - less.width - Style.space(12)
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter
            }

            Button {
              id: less
              text: "Show less"
              bordered: true
              foreground: root.foreground
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.setStackOpen(group.modelData.key, false)
            }
          }

          SwipeCard {
            visible: !group.open
            rows: group.modelData.rows
            onTapped: if (rows.length > 1) root.setStackOpen(group.modelData.key, true)
            onRemoved: root.remove(rows, false)
          }

          Repeater {
            model: group.open ? group.modelData.rows : []
            delegate: SwipeCard {
              required property var modelData
              rows: [modelData]
              onRemoved: root.remove(rows, false)
            }
          }
        }
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

  // A card, or a stack of one app's cards drawn as Omarchy's deck draws them
  // (the front card over plates that drop and shrink behind it). A finger
  // swiping it sideways takes it along; let go far enough or flicked, it
  // slides off and the list closes up, otherwise it springs back.
  component SwipeCard: Item {
    id: card
    objectName: "notificationCard"

    // The rows it stands for, newest first.
    property var rows: []
    readonly property bool stacked: rows.length > 1
    readonly property var deck: root.feed ? root.feed.deck : ({ peek: 10, shrink: 0.035, visible: 3, fade: 0.55 })
    readonly property int behind: stacked ? Math.min(rows.length, deck.visible) - 1 : 0
    readonly property real frontHeight: toast.item ? toast.item.height : 0

    // How far the finger has it, and how much of its height is left as it goes.
    property real offset: 0
    property real kept: 1

    signal tapped()
    signal removed()

    // The toast is made once its file is known (the shell hands the plugin
    // Omarchy's path after making it) and the card has a width: the drawer
    // can lay its cards out before it has one, and a toast first laid out
    // narrower than nothing never comes right.
    function load() {
      if (!toast.item && root.feed && root.feed.toastUrl && card.rows.length && card.width > 0)
        toast.setSource(root.feed.toastUrl, { row: card.rows[0], expanded: true, cardWidth: card.width, now: root.now })
    }
    onWidthChanged: load()
    Connections {
      target: root.feed
      function onToastUrlChanged() { card.load() }
    }

    width: list.width
    height: face.height * kept

    function letGo(speed) {
      var away = Math.abs(offset) > width * 0.35 || (Math.abs(speed) > 700 && speed * offset > 0)
      if (away) {
        settle.to = offset < 0 ? -width * 1.1 : width * 1.1
        settle.easing.type = Easing.OutCubic
        settle.duration = 160
        settle.leaving = true
      } else {
        settle.to = 0
        settle.easing.type = Easing.OutBack
        settle.duration = 260
        settle.leaving = false
      }
      settle.restart()
    }

    NumberAnimation {
      id: settle
      property bool leaving: false
      target: card
      property: "offset"
      onFinished: if (leaving) close.start()
    }

    NumberAnimation {
      id: close
      target: card
      property: "kept"
      to: 0
      duration: 180
      easing.type: Easing.InOutQuad
      onFinished: card.removed()
    }

    Item {
      id: face
      x: card.offset
      width: parent.width
      height: card.frontHeight + card.behind * card.deck.peek
      opacity: 1 - Math.min(0.6, Math.abs(card.offset) / card.width)

      Repeater {
        model: card.behind

        BorderSurface {
          required property int index
          readonly property int depth: index + 1
          y: depth * card.deck.peek
          z: -depth
          width: face.width
          height: card.frontHeight
          scale: Math.max(0.7, 1 - depth * card.deck.shrink)
          transformOrigin: Item.Top
          opacity: depth === card.deck.visible - 1 ? card.deck.fade : 0.85
          radius: Style.cornerRadius
          color: Commons.Color.notifications.background
          borderSpec: Border.surfaceSpec("notifications", "border", Commons.Color.notifications.border,
                                         Math.max(1, Style.space(2)))
        }
      }

      // Open as in Omarchy's expanded deck: the whole body. Its own pointer
      // handling is off; this card's handlers take the finger.
      Loader {
        id: toast
        enabled: false
        Component.onCompleted: card.load()
      }

      Binding {
        target: toast.item
        property: "cardWidth"
        value: card.width
        when: !!toast.item
      }
      Binding {
        target: toast.item
        property: "now"
        value: root.now
        when: !!toast.item
      }

      // How many more the stack holds.
      Rectangle {
        visible: card.stacked
        anchors.horizontalCenter: parent.horizontalCenter
        y: card.frontHeight - height / 2
        z: 1
        width: more.implicitWidth + Style.space(16)
        height: more.implicitHeight + Style.space(6)
        radius: height / 2
        color: Commons.Color.notifications.background
        border.color: Commons.Color.notifications.border
        border.width: Math.max(1, Style.space(1))

        Text {
          id: more
          anchors.centerIn: parent
          textFormat: Text.PlainText
          text: (card.rows.length - 1) + " more"
          color: Commons.Color.notifications.text
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
          font.bold: true
        }
      }
    }

    DragHandler {
      target: null
      yAxis.enabled: false
      // Once it has the finger, the list doesn't take it to scroll.
      grabPermissions: PointerHandler.CanTakeOverFromAnything
      property real speed: 0
      onActiveChanged: {
        if (active) {
          settle.stop()
          speed = 0
        } else {
          card.letGo(speed)
        }
      }
      onActiveTranslationChanged: {
        card.offset = activeTranslation.x
        speed = centroid.velocity.x
      }
    }

    TapHandler {
      onTapped: card.tapped()
    }
  }
}
