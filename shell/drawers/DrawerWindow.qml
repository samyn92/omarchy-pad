import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// One screen's drawers. The window covers the screen above everything, but
// is see-through and takes no input until a drawer comes down on it: then a
// tap beside the card or a push up puts it away. The card is Omarchy's popup
// card (Ui/KeyboardPanel.qml's surface), sliding out from under the bar.
PanelWindow {
  id: window

  required property var modelData
  property var drawers: null

  readonly property bool active: !!drawers && drawers.side !== "" && drawers.activeScreen === modelData
  readonly property string side: active ? drawers.side : ""

  // The drawers come out from under Omarchy's bar when it is along the top.
  readonly property real barEdge: {
    var bar = drawers && drawers.shell ? drawers.shell.bar : null
    if (!bar || bar.barHidden || bar.position !== "top") return 0
    return typeof bar.barSizeFor === "function" ? bar.barSizeFor(modelData.name) : bar.barSize
  }
  readonly property int margin: Style.gapsOut
  readonly property real maxCardHeight: Math.max(Style.space(120), height - barEdge - margin * 2)
  readonly property real cardWidth: Math.min(Style.space(400), width - margin * 2)

  readonly property Item card: drawers && drawers.side === "left" ? notificationsCard : quickSettingsCard
  // How far the drawer travels from closed to open: its card and the gap
  // above it.
  readonly property real openHeight: card.height + margin

  // What Omarchy's panels read their colours from when they hang off the bar.
  readonly property QtObject palette: QtObject {
    readonly property color foreground: Commons.Color.popups.text
    readonly property color background: Commons.Color.popups.background
    readonly property string fontFamily: Style.font.family
  }

  screen: modelData
  color: "transparent"
  exclusionMode: ExclusionMode.Ignore
  anchors { top: true; left: true; right: true; bottom: true }

  WlrLayershell.namespace: "omarchy-pad-drawers"
  WlrLayershell.layer: WlrLayer.Overlay
  // Touch needs no keyboard, and taking it would move the text field focus
  // (and floatkey) away from the window underneath.
  WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

  readonly property Region everywhere: Region { width: window.width; height: window.height }
  readonly property Region nowhere: Region {}
  mask: active ? everywhere : nowhere

  Item {
    id: track
    y: window.barEdge
    width: parent.width
    height: parent.height - y
    // The card slides out from under the bar's edge rather than over it.
    clip: true
    // Hidden by opacity, not visibility: positioners skip invisible items, so
    // a hidden card would not know its height when begin() asks for it.
    opacity: window.active ? 1 : 0

    // Behind the card: a tap beside it closes the drawer, and a push up from
    // anywhere the controls don't take it moves the drawer with the finger.
    MouseArea {
      id: push
      anchors.fill: parent
      enabled: window.active

      property bool pushing: false
      property real startY: 0
      property real startReveal: 0
      property real lastY: 0
      property double lastTime: 0
      // px/s, negative upward.
      property real velocity: 0

      onPressed: function(mouse) {
        pushing = false
        startY = lastY = mouse.y
        lastTime = Date.now()
        velocity = 0
      }

      onPositionChanged: function(mouse) {
        if (!pushing) {
          if (Math.abs(mouse.y - startY) < Style.space(10)) return
          pushing = true
          startY = mouse.y
          startReveal = window.drawers.reveal
        }
        var now = Date.now()
        var step = (mouse.y - lastY) * 1000 / Math.max(1, now - lastTime)
        velocity = velocity * 0.6 + step * 0.4
        lastY = mouse.y
        lastTime = now
        window.drawers.follow(startReveal + mouse.y - startY)
      }

      // Up past a quarter of the drawer, or flicked up, and it goes;
      // otherwise it springs back.
      onReleased: function(mouse) {
        if (pushing) {
          pushing = false
          var pushedUp = startReveal - window.drawers.reveal
          window.drawers.settle(!(velocity < -700 || pushedUp > window.openHeight * 0.25))
          return
        }
        var inside = window.card.mapFromItem(push, mouse.x, mouse.y)
        if (!window.card.contains(inside)) window.drawers.close()
      }

      onCanceled: {
        if (pushing) window.drawers.settle(true)
        pushing = false
      }
    }

    DrawerCard {
      id: notificationsCard
      opacity: window.side === "left" ? 1 : 0
      enabled: window.side === "left"
      x: window.margin
      contentHeight: notifications.implicitHeight

      Notifications {
        id: notifications
        width: parent.width
        feed: window.drawers ? window.drawers.notifications : null
        settings: window.drawers ? window.drawers.quickSettings : null
        palette: window.palette
        shown: window.side === "left"
        maxHeight: window.maxCardHeight - notificationsCard.verticalInset
      }
    }

    DrawerCard {
      id: quickSettingsCard
      opacity: window.side === "right" ? 1 : 0
      enabled: window.side === "right"
      x: track.width - width - window.margin
      contentHeight: quickSettings.implicitHeight

      QuickSettings {
        id: quickSettings
        width: parent.width
        settings: window.drawers ? window.drawers.quickSettings : null
        palette: window.palette
        onCloseRequested: window.drawers.close()
      }
    }
  }

  component DrawerCard: BorderSurface {
    id: drawerCard

    property real contentHeight: 0
    readonly property real verticalInset: contentTopInset + contentBottomInset
    default property alias content: holder.data

    width: window.cardWidth
    height: Math.min(window.maxCardHeight, contentHeight + verticalInset)
    // Down by however far the finger has pulled, its bottom edge leading.
    y: (window.drawers ? window.drawers.reveal : 0) - window.openHeight + window.margin
    color: Commons.Color.popups.background
    borderSpec: Border.surfaceSpec("popups", "border", Commons.Color.popups.border, Math.max(1, Style.space(2)))
    padding: Style.spacing.popupPadding
    radius: Style.cornerRadius

    Item {
      id: holder
      x: drawerCard.contentLeftInset
      y: drawerCard.contentTopInset
      width: drawerCard.width - drawerCard.contentLeftInset - drawerCard.contentRightInset
      height: drawerCard.height - drawerCard.verticalInset
      LayoutMirroring.enabled: Commons.I18n.rightToLeft
      LayoutMirroring.childrenInherit: true
    }
  }
}
