import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The quick settings drawer's second page: Omarchy's agents panel, the one
// its bar opens from the robot. The panel is Omarchy's own, loaded from where
// Omarchy keeps it, and made here unseen for what it knows (the usage records
// it watches, sign-ins, the agents it starts); what its popup would show is
// shown on this page instead, so it looks and works as Omarchy's does. Only a
// few of its names are relied on (`opened`, `open`, `close`, its popup's
// `content`); if those change, the page stays empty and nothing else breaks.
Item {
  id: page
  objectName: "agentsPage"

  property string omarchyPath: ""
  // On screen: the panel's clocks and its refresh run only then.
  property bool shown: false
  // The panel is done (it started an agent, say): the drawer goes away.
  signal closeRequested()

  readonly property string panelUrl: omarchyPath ? "file://" + omarchyPath + "/shell/plugins/agents/Panel.qml" : ""
  property Item content: null

  implicitHeight: content ? content.implicitHeight : Style.space(120)

  // What a bar hands its widgets that this panel reads, its unseen button
  // and popup included.
  QtObject {
    id: bar
    readonly property color foreground: Commons.Color.popups.text
    readonly property color barForeground: Commons.Color.popups.text
    readonly property color urgent: Commons.Color.urgent
    readonly property string fontFamily: Style.font.family
    readonly property string position: "top"
    readonly property bool vertical: false
    readonly property int barSize: Style.bar.sizeHorizontal
    readonly property bool foregroundAnimationEnabled: false
    function run(command) { if (command) Util.execDetached(command) }
  }

  // Its settings as the bar would give them: the widget's defaults, under
  // whatever is set for it in Omarchy's bar layout.
  FileView {
    id: manifestFile
    path: page.omarchyPath ? page.omarchyPath + "/shell/plugins/agents/manifest.json" : ""
    blockLoading: true
    printErrors: false
  }
  FileView {
    id: shellConfig
    path: Quickshell.env("HOME") + "/.config/omarchy/shell.json"
    blockLoading: true
    printErrors: false
  }
  function settings() {
    var out = {}
    try {
      var defaults = JSON.parse(manifestFile.text()).barWidget.defaults
      for (var key in defaults) out[key] = defaults[key]
    } catch (e) {}
    try {
      var layout = JSON.parse(shellConfig.text()).bar.layout
      for (var section in layout) {
        var entries = layout[section] || []
        for (var i = 0; i < entries.length; i++) {
          if (!entries[i] || entries[i].id !== "omarchy.agents") continue
          for (var k in entries[i]) if (k !== "id") out[k] = entries[i][k]
        }
      }
    } catch (e) {}
    return out
  }

  // The panel's popup, found by what only it has; it is kept shut, as its
  // content lives here.
  function popupOf(panel) {
    for (var i = 0; i < panel.data.length; i++) {
      var candidate = panel.data[i]
      if (candidate && candidate.content !== undefined && candidate.anchorItem !== undefined) return candidate
    }
    return null
  }

  Loader {
    id: panelLoader
    active: page.panelUrl !== ""
    source: page.panelUrl
    visible: false
    onLoaded: {
      item.bar = bar
      item.settings = page.settings()
      var popup = page.popupOf(item)
      if (!popup || !popup.content) {
        console.warn("omarchy-pad.drawers: Omarchy's agents panel has no popup content to show")
        return
      }
      popup.open = false
      page.content = popup.content.createObject(holder)
      if (page.shown) item.open()
    }
  }

  onShownChanged: {
    var panel = panelLoader.item
    if (!panel) return
    if (shown) panel.open()
    else panel.close()
  }

  Connections {
    target: panelLoader.item
    ignoreUnknownSignals: true
    function onOpenedChanged() {
      if (page.shown && panelLoader.item && !panelLoader.item.opened) page.closeRequested()
    }
  }

  Item {
    id: holder
    width: parent.width
    height: page.implicitHeight
  }
}
