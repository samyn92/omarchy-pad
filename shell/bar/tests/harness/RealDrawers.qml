import Quickshell
import "plugins/drawers"

// The drawers plugin, for tests/try --drawers, which copies it in beside it.
// Loaded through this file so the plugin's folder is imported, as the shell
// imports it, and its own types resolve. The harness hands it the bar.
Drawers {
  omarchyPath: Quickshell.env("OMARCHY_PATH") || "/usr/share/omarchy"
  manifest: ({ id: "omarchy-pad.drawers" })
}
