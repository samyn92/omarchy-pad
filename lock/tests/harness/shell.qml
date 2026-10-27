import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Pam
import Quickshell.Wayland
import "plugins/lock"

// Locks a nested Hyprland with the lock plugin's view, wired the way
// Omarchy's lock service wires it, but against a throwaway PAM service whose
// password is "test": the real one counts failures toward faillock.
ShellRoot {
  id: harness

  property string password: ""
  property string pending: ""
  property bool authenticating: false
  property string failure: ""
  // The view on the (single) nested output, for the state call.
  property Item view: null

  WlSessionLock {
    id: sessionLock

    WlSessionLockSurface {
      color: "black"

      LockView {
        anchors.fill: parent
        Component.onCompleted: harness.view = this
        backgroundPath: Quickshell.env("LOCK_TEST_BACKGROUND") || ""
        loadBackground: sessionLock.locked
        inputEnabled: sessionLock.locked
        authenticatingPassword: harness.authenticating
        failureMessage: harness.failure
        passwordText: harness.password
        onPasswordTextEdited: function(password) { harness.password = password }
        onSubmitPassword: function(password) {
          harness.pending = password
          harness.authenticating = true
          pam.start()
        }
        onClearFailureRequested: harness.failure = ""
      }
    }
  }

  PamContext {
    id: pam
    configDirectory: Quickshell.env("LOCK_TEST_PAM")
    config: "lock-test"
    onResponseRequiredChanged: if (responseRequired) respond(harness.pending)
    onCompleted: function(result) {
      harness.authenticating = false
      harness.pending = ""
      if (result === PamResult.Success) sessionLock.locked = false
      else harness.failure = "Wrong password"
    }
  }

  IpcHandler {
    target: "harness"

    function lock(): void { sessionLock.locked = true }
    function unlock(): void { sessionLock.locked = false }
    function state(): string {
      var view = harness.view
      if (!view) return JSON.stringify({ locked: sessionLock.locked })
      var keyboard = view.children.filter(c => c.objectName === "lockKeyboard")[0]
      return JSON.stringify({
        locked: sessionLock.locked,
        secure: sessionLock.secure,
        typed: harness.password,
        failure: harness.failure,
        hardwareKeyboard: view.hardwareKeyboard,
        keyboardOpen: view.keyboardOpen,
        keyboardChoice: view.keyboardChoice,
        layouts: view.layoutCodes,
        layout: keyboard ? keyboard.layoutCode : "",
        keyboardY: keyboard ? keyboard.y : -1,
        keyboardHeight: keyboard ? keyboard.height : -1,
        width: view.width,
        height: view.height
      })
    }
  }
}
