import QtQuick
import Quickshell
import Quickshell.Io

// The notifications on screen and the recent ones, as Omarchy's notification
// service last wrote them. Both files are Omarchy's: they are read here, never
// written. Rows go through the service's own Store.js, the way it reads them
// back after a restart, so a drawer card carries exactly what a toast would.
Scope {
  id: root

  property string omarchyPath: ""
  readonly property string stateDir: Quickshell.env("HOME") + "/.local/state/omarchy/notifications"
  readonly property string notificationsDir: root.omarchyPath ? root.omarchyPath + "/shell/plugins/notifications" : ""
  readonly property string toastUrl: root.notificationsDir ? "file://" + root.notificationsDir + "/Toast.qml" : ""

  // Newest first.
  property var live: []
  property var recent: []
  // How many recent ones the drawer draws; history keeps up to 100.
  property int recentLimit: 30

  // Store.js has no home outside Omarchy's notifications plugin and imports
  // its neighbours, so it is loaded from there, wherever OMARCHY_PATH is.
  property var store: null
  onNotificationsDirChanged: {
    if (root.store) root.store.destroy()
    root.store = null
    root.parsed = ""
    if (!root.notificationsDir) return
    root.store = Qt.createQmlObject(
      'import QtQuick; import "file://' + root.notificationsDir + '/Store.js" as Store; QtObject {'
      + ' function live(text) { return Store.parseLive(text).map(Store.normalise).reverse() }'
      + ' function history(text) { return Store.parseHistory(text).map(Store.normalise) } }',
      root, "NotificationStore")
    root.parse()
  }

  function refresh() {
    liveFile.reload()
    historyFile.reload()
  }

  // Rebuilding the cards for unchanged files would lay the drawer out again
  // just as begin() measures it.
  property string parsed: ""
  function parse() {
    if (!root.store) return
    var key = liveFile.contents + "\u0000" + historyFile.contents
    if (key === root.parsed) return
    root.parsed = key
    root.live = root.store.live(liveFile.contents)
    root.recent = root.store.history(historyFile.contents).slice(0, root.recentLimit)
  }

  // Small files, read in place so a pull that just began already has the
  // drawer's final height.
  FileView {
    id: liveFile
    path: root.stateDir + "/live.json"
    blockLoading: true
    watchChanges: true
    printErrors: false
    property string contents: ""
    onFileChanged: reload()
    onLoaded: { contents = text(); root.parse() }
    onLoadFailed: { contents = ""; root.parse() }
  }

  FileView {
    id: historyFile
    path: root.stateDir + "/history.json"
    blockLoading: true
    watchChanges: true
    printErrors: false
    property string contents: ""
    onFileChanged: reload()
    onLoaded: { contents = text(); root.parse() }
    onLoadFailed: { contents = ""; root.parse() }
  }
}
