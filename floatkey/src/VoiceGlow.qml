import QtQuick
import QtQuick.Effects

// While Superwhisper listens the keyboard itself glows, the way Superwhisper's
// edge glow lights the screen: the docked keyboard lights the space just above
// its top edge, the floating one gets a halo. It swells with the voice and
// breathes while the words are worked out.
Item {
    id: glow

    required property QtObject style
    property bool compact: false
    property real radius: 0
    property string phase: dictation.state
    property real level: dictation.level

    readonly property bool live: phase === "listening"
    readonly property bool busy: phase === "working"

    property real t: 0
    readonly property real breath: 0.5 + 0.5 * Math.sin(t * Math.PI * 2 / 1.3)
    readonly property real target: live ? 0.35 + 0.65 * level
                                 : busy ? 0.2 + 0.25 * breath
                                 : phase === "done" ? 0.4 : 0
    property real intensity: target
    Behavior on intensity { NumberAnimation { duration: glow.live ? 90 : 400 } }

    visible: intensity > 0.01

    FrameAnimation {
        running: glow.visible || glow.live || glow.busy
        onTriggered: glow.t += frameTime
    }

    // Docked: light rising off the top edge, and the edge itself lit.
    Rectangle {
        visible: !glow.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.top
        height: 24 + 48 * glow.intensity
        opacity: glow.intensity
        gradient: Gradient {
            GradientStop { position: 0; color: glow.style.alpha(glow.style.accent, 0) }
            GradientStop { position: 0.7; color: glow.style.alpha(glow.style.accent, 0.18) }
            GradientStop { position: 1; color: glow.style.alpha(glow.style.accentPartner, 0.5) }
        }
    }
    Rectangle {
        visible: !glow.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2
        opacity: Math.min(1, glow.intensity * 1.6)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: glow.style.accent }
            GradientStop { position: 0.5 + 0.4 * Math.sin(glow.t * 1.7); color: glow.style.accentPartner }
            GradientStop { position: 1; color: glow.style.accent }
        }
    }

    // Floating: a halo around the board.
    Rectangle {
        id: halo
        anchors.fill: parent
        radius: glow.radius
        visible: false
        layer.enabled: glow.compact
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: glow.style.accent }
            GradientStop { position: 0.5 + 0.4 * Math.sin(glow.t * 1.7); color: glow.style.accentPartner }
            GradientStop { position: 1; color: glow.style.accent }
        }
    }
    MultiEffect {
        visible: glow.compact
        source: halo
        anchors.fill: halo
        autoPaddingEnabled: true
        blurEnabled: true
        blurMax: 48
        blur: 1
        opacity: glow.intensity * 0.9
        scale: 1 + 0.025 * glow.intensity
    }
}
