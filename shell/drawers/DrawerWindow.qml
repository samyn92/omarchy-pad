import QtQuick
import QtQuick.Effects
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
  // The same for the right drawer's first page, quick settings.
  readonly property real firstPageHeight: Math.min(maxCardHeight, quickSettings.implicitHeight + quickSettingsCard.verticalInset) + margin
  readonly property real turn: drawers ? drawers.turn : 0

  // A finger moving the open drawer up or down, from the space beside it or
  // anywhere on quick settings: the drawer follows it (and on past quick
  // settings, turns to the agents); on the agents, a push up turns back.
  property bool pushing: false
  property real pushReveal: 0
  property real pushLast: 0
  property double pushTime: 0
  // px/s, negative upward.
  property real pushVelocity: 0

  function startPush() {
    pushing = true
    pushReveal = drawers.reveal
    pushLast = 0
    pushTime = Date.now()
    pushVelocity = 0
  }

  function movePush(dy) {
    var now = Date.now()
    var step = (dy - pushLast) * 1000 / Math.max(1, now - pushTime)
    pushVelocity = pushVelocity * 0.6 + step * 0.4
    pushLast = dy
    pushTime = now
    if (drawers.page === 1) drawers.turnBack(-dy)
    else drawers.follow(pushReveal + dy)
  }

  // Up past a quarter of the drawer, or flicked up, and it goes; otherwise
  // it springs back. On the agents it turns back a page instead, as a flick
  // up does.
  function endPush() {
    pushing = false
    if (drawers.page === 1) {
      if (pushVelocity < -700) drawers.turnBack(drawers.turnDistance)
      drawers.settle(true)
      return
    }
    // A page part turned: the card took the next page's height on the way,
    // which isn't a push up.
    if (drawers.turn > 0) {
      drawers.settle(true)
      return
    }
    var pushedUp = pushReveal - drawers.reveal
    drawers.settle(!(pushVelocity < -700 || pushedUp > openHeight * 0.25))
  }

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

      property real startY: 0

      onPressed: function(mouse) {
        startY = mouse.y
        window.pushing = false
      }

      onPositionChanged: function(mouse) {
        if (!window.pushing && Math.abs(mouse.y - startY) < Style.space(10)) return
        if (!window.pushing) {
          window.startPush()
          startY = mouse.y
        }
        window.movePush(mouse.y - startY)
      }

      onReleased: function(mouse) {
        if (window.pushing) {
          window.endPush()
          return
        }
        var inside = window.card.mapFromItem(push, mouse.x, mouse.y)
        if (!window.card.contains(inside)) window.drawers.close()
      }

      onCanceled: if (window.pushing) window.endPush()
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
      // Between the two pages' heights as it turns.
      contentHeight: quickSettings.implicitHeight + (agents.implicitHeight - quickSettings.implicitHeight) * window.turn

      // Both pages, the one turned to in view: quick settings goes down and
      // out as the agents come in from the top.
      Item {
        width: parent.width
        height: parent.height
        clip: true

        QuickSettings {
          id: quickSettings
          y: window.turn * parent.height
          width: parent.width
          settings: window.drawers ? window.drawers.quickSettings : null
          palette: window.palette
          onCloseRequested: window.drawers.close()
        }

        AgentsPage {
          id: agents
          y: (window.turn - 1) * implicitHeight
          width: parent.width
          omarchyPath: window.drawers ? window.drawers.omarchyPath : ""
          shown: window.side === "right" && window.turn > 0
          onCloseRequested: window.drawers.close()
        }
      }

      // The whole card is the drawer's to move with a finger going up or
      // down, as the space beside it is: a tap still reaches the control
      // under it, and a finger going sideways a slider.
      Item {
        x: -quickSettingsCard.contentLeftInset
        y: -quickSettingsCard.contentTopInset
        width: quickSettingsCard.width
        height: quickSettingsCard.height

        DragHandler {
          target: null
          xAxis.enabled: false
          grabPermissions: PointerHandler.CanTakeOverFromAnything
          enabled: window.side === "right"
          onActiveChanged: {
            if (active) window.startPush()
            else if (window.pushing) window.endPush()
          }
          onActiveTranslationChanged: if (active) window.movePush(activeTranslation.y)
          onCanceled: if (window.pushing) window.endPush()
        }
      }

      // Which page it is on, as dots down its right edge; a tap on one turns
      // to it.
      Column {
        // In the card's right padding (this is inside its content, which
        // starts after the left one).
        x: quickSettingsCard.width - quickSettingsCard.contentLeftInset - quickSettingsCard.contentRightInset / 2 - width / 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.space(6)

        Repeater {
          model: 2

          Rectangle {
            required property int index
            // The agents are above quick settings, as they come from the top.
            readonly property int pageOf: 1 - index
            width: Style.space(6)
            height: width
            radius: width / 2
            color: window.palette.foreground
            opacity: 0.3 + 0.7 * (pageOf === 1 ? window.turn : 1 - window.turn)

            TapHandler {
              margin: Style.space(10)
              onTapped: window.drawers.showPage(parent.pageOf)
            }
          }
        }
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

    // Omarchy's popup cards lie flat, as on a desktop they open beside what
    // they belong to. A drawer comes down over whatever window is there, so
    // it stands off it a little, as a raised card does: a wide soft shadow
    // and a tight one along its edge. Darker on a dark theme, where a faint
    // one would vanish against dark windows.
    readonly property bool darkTheme: color.hslLightness < 0.5
    RectangularShadow {
      z: -1
      anchors.fill: parent
      radius: drawerCard.radius
      offset.y: Style.space(10)
      blur: Style.space(40)
      spread: Style.space(2)
      color: Qt.rgba(0, 0, 0, drawerCard.darkTheme ? 0.55 : 0.28)
    }
    RectangularShadow {
      z: -1
      anchors.fill: parent
      radius: drawerCard.radius
      offset.y: Style.space(2)
      blur: Style.space(6)
      color: Qt.rgba(0, 0, 0, drawerCard.darkTheme ? 0.4 : 0.14)
    }

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
