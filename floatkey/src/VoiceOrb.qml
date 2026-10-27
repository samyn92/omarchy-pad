import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// The voice orb: tap it and speak, tap again and what was said is typed; or
// hold it while talking and let go. Asleep it is a quiet key with a
// microphone. Listening, it lights up in the theme's accent, swirls, and its
// glow swells with the voice; while Superwhisper works the words out it
// breathes, and a tick shows as they go in. A touch squeezes it, flashes its
// glow and sends waves out, and it springs back. The docked keyboard's mic
// key is the same orb in the shape of a key, driven by the keyboard's own
// touch handling (`interactive: false`).
Item {
    id: orb

    required property QtObject style
    property string phase: dictation.state
    property real level: dictation.level
    property real radius: Math.min(width, height) / 2
    property bool interactive: true
    property bool down: area.pressed
    property color idleColor: style.key
    property color iconColor: style.label
    property real iconSize: Math.min(width, height) * 0.56

    readonly property bool live: phase === "listening"
    readonly property bool busy: phase === "working"
    readonly property bool done: phase === "done"
    readonly property bool lit: phase !== "idle"

    readonly property color accent: style.accent
    readonly property color partner: style.accentPartner
    readonly property color light: style.mix(accent, style.white, 0.5)

    // The voice, eased so the glow moves like breath rather than flicker.
    property real voice: live ? level : 0
    Behavior on voice { NumberAnimation { duration: 90 } }

    property real litness: lit ? 1 : 0
    Behavior on litness { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

    // One clock drives the swirl and the breathing.
    property real t: 0
    property real spin: 0
    property real speed: busy ? 330 : live ? 70 + 160 * voice : 40
    Behavior on speed { NumberAnimation { duration: 400 } }
    readonly property real breath: 0.5 + 0.5 * Math.sin(t * Math.PI * 2 / 1.3)

    FrameAnimation {
        running: orb.visible && (orb.lit || orb.litness > 0)
        onTriggered: {
            orb.t += frameTime
            orb.spin = (orb.spin + frameTime * orb.speed) % 360
        }
    }

    // A touch: full while the finger is down, fading after it lifts.
    property real touch: 0
    Behavior on touch {
        enabled: !orb.down
        NumberAnimation { duration: 450; easing.type: Easing.OutCubic }
    }
    onDownChanged: {
        touch = down ? 1 : 0
        if (down) {
            wave(1)
            burst.start()
        }
    }

    readonly property real glow: Math.max(live ? 0.45 + 0.55 * voice
                                          : busy ? 0.25 + 0.3 * breath
                                          : done ? 0.55 : 0, 0.55 * touch)
    property real shownGlow: glow
    Behavior on shownGlow { NumberAnimation { duration: orb.live || orb.down ? 80 : 350 } }

    // Squeezed under the finger, and a spring back when it lifts.
    scale: down ? 0.86 : 1 + (live ? 0.06 * voice : busy ? 0.02 * breath : 0)
    Behavior on scale { SpringAnimation { spring: 6; damping: 0.26; epsilon: 0.002 } }

    // ---- the glow --------------------------------------------------------

    // Two blobs in the two colours circle each other under a blur.
    Item {
        id: glowSource
        anchors.fill: parent
        visible: false
        layer.enabled: true

        readonly property real reach: Math.min(orb.width, orb.height) * (0.1 + 0.12 * orb.voice)
        Rectangle {
            width: orb.width
            height: orb.height
            radius: orb.radius
            x: glowSource.reach * Math.cos(orb.spin * Math.PI / 180)
            y: glowSource.reach * Math.sin(orb.spin * Math.PI / 180)
            color: orb.accent
        }
        Rectangle {
            width: orb.width
            height: orb.height
            radius: orb.radius
            x: -glowSource.reach * Math.cos(orb.spin * Math.PI / 180)
            y: -glowSource.reach * Math.sin(orb.spin * Math.PI / 180)
            color: orb.partner
            opacity: 0.85
        }
    }

    MultiEffect {
        source: glowSource
        anchors.fill: glowSource
        visible: orb.shownGlow > 0.01
        autoPaddingEnabled: true
        blurEnabled: true
        blurMax: 32
        blur: 1
        opacity: orb.shownGlow
        scale: 1.05 + (orb.live ? 0.4 * orb.voice : orb.busy ? 0.08 * orb.breath : 0.1)
    }

    // Waves that run out from its edge: a burst of them at a touch, and a
    // steady swell while it listens, closer and stronger the louder the voice.
    // A few rings are reused in turn.
    property int nextWave: 0
    function wave(strength) {
        const ring = waves.itemAt(nextWave)
        if (!ring || !visible)
            return
        ring.fire(strength)
        nextWave = (nextWave + 1) % waves.count
    }

    Repeater {
        id: waves
        model: 6
        Item {
            id: w
            property real grow: 0
            property real strength: 1
            // How far out it travels: most of the orb's size, less for the
            // docked keyboard's wide mic key.
            readonly property real spread: Math.min(orb.width, orb.height) * (0.4 + 0.6 * strength)
            anchors.centerIn: parent
            width: orb.width + 2 * spread * grow
            height: orb.height + 2 * spread * grow
            visible: fade.running

            function fire(s) {
                strength = s
                fade.restart()
            }

            Rectangle {
                anchors.fill: parent
                radius: orb.radius + w.spread * w.grow
                readonly property real remaining: 1 - w.grow
                color: orb.style.alpha(orb.accent, (0.1 + 0.14 * w.strength) * remaining)
                border.width: 1.5 + 2.5 * w.strength * remaining
                border.color: orb.style.alpha(w.grow < 0.5 ? orb.accent : orb.partner, (0.55 + 0.4 * w.strength) * remaining)
            }

            NumberAnimation {
                id: fade
                target: w
                property: "grow"
                from: 0
                to: 1
                duration: 520 + 380 * w.strength
                easing.type: Easing.OutCubic
            }
        }
    }

    Timer {
        id: burst
        interval: 130
        onTriggered: orb.wave(0.6)
    }

    // While it listens, a wave every so often, sooner with a louder voice.
    Timer {
        running: orb.live && orb.visible
        repeat: true
        interval: 1000 - 600 * orb.voice
        onTriggered: orb.wave(0.45 + 0.55 * orb.voice)
    }

    // ---- the body --------------------------------------------------------

    Rectangle {
        anchors.fill: parent
        radius: orb.radius
        color: orb.idleColor
        opacity: 1 - orb.litness
    }

    Shape {
        anchors.fill: parent
        opacity: orb.litness
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: ConicalGradient {
                centerX: orb.width / 2
                centerY: orb.height / 2
                angle: orb.spin
                GradientStop { position: 0; color: orb.accent }
                GradientStop { position: 0.35; color: orb.partner }
                GradientStop { position: 0.6; color: orb.light }
                GradientStop { position: 1; color: orb.accent }
            }
            PathRectangle {
                width: orb.width
                height: orb.height
                radius: orb.radius
            }
        }
    }

    // A soft highlight across the top, so it reads as round.
    Rectangle {
        anchors.fill: parent
        radius: orb.radius
        opacity: 0.2 + orb.litness * 0.3
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.35) }
            GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0) }
        }
    }

    // ---- what is inside --------------------------------------------------

    KeyIcon {
        anchors.centerIn: parent
        width: orb.iconSize
        height: width
        name: "mic"
        color: orb.iconColor
        opacity: orb.phase === "idle" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }

    // Listening: bars that dance with the voice.
    Row {
        id: bars
        anchors.centerIn: parent
        spacing: Math.max(2, Math.round(barWidth * 0.8))
        opacity: orb.live ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        readonly property real barWidth: Math.max(2, Math.round(Math.min(orb.width, orb.height) * 0.085))
        readonly property real tall: Math.min(orb.width, orb.height) * 0.5
        readonly property var shape: [0.55, 1, 0.8, 0.45]

        Repeater {
            model: 4
            Rectangle {
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                width: bars.barWidth
                radius: width / 2
                height: Math.max(width, bars.tall * (0.22 + 0.78 * orb.voice * bars.shape[index]
                                                     * (0.65 + 0.35 * Math.sin(orb.t * (7 + index * 2.3) + index))))
                color: orb.style.accentText
            }
        }
    }

    // Working: three dots in a wave.
    Row {
        anchors.centerIn: parent
        spacing: dot * 0.7
        opacity: orb.busy ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        readonly property real dot: Math.max(3, Math.round(Math.min(orb.width, orb.height) * 0.11))

        Repeater {
            model: 3
            Rectangle {
                required property int index
                width: parent.dot
                height: width
                radius: width / 2
                color: orb.style.accentText
                opacity: 0.35 + 0.65 * Math.max(0, Math.sin(orb.t * 6 - index * 0.9))
            }
        }
    }

    KeyIcon {
        anchors.centerIn: parent
        width: orb.iconSize
        height: width
        name: "check"
        stroke: 2.2
        color: orb.style.accentText
        opacity: orb.done ? 1 : 0
        scale: orb.done ? 1 : 0.6
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
    }

    // ---- touch -----------------------------------------------------------

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -6
        enabled: orb.interactive && dictation.available
        preventStealing: true
        pressAndHoldInterval: 350
        property bool talking: false
        onPressed: talking = false
        // Hold to talk, let go to type it.
        onPressAndHold: {
            if (dictation.state === "idle" || dictation.state === "done") {
                talking = true
                dictation.start()
            }
        }
        onReleased: if (talking) dictation.stop()
        onCanceled: if (talking) dictation.stop()
        onClicked: if (!talking) dictation.toggle()
    }
}
