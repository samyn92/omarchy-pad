import QtQuick
import Quickshell
import Quickshell.Io

// The notifications on screen and the recent ones, as Omarchy's notification
// service last wrote them. Both files are Omarchy's: they are read here, never
// written. Rows go through the service's own Store.js, the way it reads them
// back after a restart, so a drawer card carries exactly what a toast would,
// and are stacked by its Layout.js, as its toasts are.
//
// What was swiped away or cleared in the drawer is left out. Omarchy keeps no
// way to remove one notification from its history, so the drawer remembers
// those in a file of its own and leaves Omarchy's alone.
Scope {
  id: root

  property string omarchyPath: ""
  property string stateDir: Quickshell.env("HOME") + "/.local/state/omarchy/notifications"
  property string hiddenFile: (Quickshell.env("XDG_STATE_HOME") || Quickshell.env("HOME") + "/.local/state")
    + "/omarchy-pad/hidden-notifications.json"
  readonly property string notificationsDir: root.omarchyPath ? root.omarchyPath + "/shell/plugins/notifications" : ""
  readonly property string toastUrl: root.notificationsDir ? "file://" + root.notificationsDir + "/Toast.qml" : ""

  // Newest first. `recent` is what the drawer draws of `allRecent`.
  property var live: []
  property var recent: []
  property var allRecent: []
  // The recent ones as Omarchy stacks them: [{ key, rows }], newest first.
  property var recentGroups: []
  // How many recent ones the drawer draws; history keeps up to 100.
  property int recentLimit: 30

  // Omarchy's collapsed deck: how far each card behind drops, how much it
  // shrinks, how many show, and how faint the last one is.
  readonly property var deck: root.store ? root.store.deck() : ({ peek: 10, shrink: 0.035, visible: 3, fade: 0.55 })

  // The rows removed in the drawer, by idOf.
  property var hidden: ({})

  // Store.js and Layout.js have no home outside Omarchy's notifications plugin
  // and import their neighbours, so they are loaded from there, wherever
  // OMARCHY_PATH is.
  property var store: null
  onNotificationsDirChanged: {
    if (root.store) root.store.destroy()
    root.store = null
    root.parsed = ""
    if (!root.notificationsDir) return
    root.store = Qt.createQmlObject(
      'import QtQuick; import "file://' + root.notificationsDir + '/Store.js" as Store;'
      + ' import "file://' + root.notificationsDir + '/Layout.js" as Layout; QtObject {'
      + ' function live(text) { return Store.parseLive(text).map(Store.normalise).reverse() }'
      + ' function history(text) { return Store.parseHistory(text).map(Store.normalise) }'
      + ' function groupKey(row) { return Layout.groupKeyFor(row) }'
      + ' function deck() { return { peek: Layout.PEEK, shrink: Layout.SHRINK, visible: Layout.VISIBLE, fade: Layout.FADE } } }',
      root, "NotificationStore")
    // Once toastUrl, which follows the same path, has caught up: the cards
    // made from the rows load their toasts from it.
    Qt.callLater(root.parse)
  }

  // A row's own, across the move from on screen to history: Omarchy keeps
  // when it came, and its source and summary, but a key only lasts a session.
  function idOf(row) {
    return String(row.ts) + "|" + String(row.app || "") + "|" + String(row.summary || "")
  }

  function groupsOf(rows) {
    var order = [], byKey = {}
    for (var i = 0; i < rows.length; i++) {
      var key = root.store ? root.store.groupKey(rows[i]) : String(rows[i].app || "?")
      if (!byKey[key]) { byKey[key] = { key: key, rows: [] }; order.push(key) }
      byKey[key].rows.push(rows[i])
    }
    return order.map(function(key) { return byKey[key] })
  }

  // Leaves these rows out from now on.
  function hide(rows) {
    var next = Object.assign({}, root.hidden)
    for (var i = 0; i < rows.length; i++) next[root.idOf(rows[i])] = true
    root.hidden = next
    root.saveHidden()
    root.parsed = ""
    root.parse()
  }

  function saveHidden() {
    hiddenView.setText(JSON.stringify(Object.keys(root.hidden)) + "\n")
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
    var live = root.store.live(liveFile.contents)
    var history = root.store.history(historyFile.contents)
    // What has left Omarchy's files (history keeps a day) is forgotten here
    // too, so the file stays small. Only when history was read: an empty one
    // may be a file that is being rewritten.
    if (historyFile.contents) {
      var present = {}, kept = {}, dropped = false
      live.concat(history).forEach(function(row) { present[root.idOf(row)] = true })
      for (var id in root.hidden) {
        if (present[id]) kept[id] = true
        else dropped = true
      }
      if (dropped) {
        root.hidden = kept
        root.saveHidden()
      }
    }
    var shown = function(row) { return !root.hidden[root.idOf(row)] }
    root.live = live.filter(shown)
    root.allRecent = history.filter(shown)
    root.recent = root.allRecent.slice(0, root.recentLimit)
    root.recentGroups = root.groupsOf(root.recent)
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

  FileView {
    id: hiddenView
    path: root.hiddenFile
    blockLoading: true
    atomicWrites: true
    printErrors: false
    onLoaded: {
      var ids = []
      try { ids = JSON.parse(text()) || [] } catch (e) {}
      var hidden = {}
      if (Array.isArray(ids)) ids.forEach(function(id) { hidden[String(id)] = true })
      root.hidden = hidden
      root.parsed = ""
      root.parse()
    }
  }

  // FileView writes the file, but not the folder it goes in.
  Process {
    running: true
    command: ["mkdir", "-p", root.hiddenFile.substring(0, root.hiddenFile.lastIndexOf("/"))]
  }
}
