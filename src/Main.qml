import QtQuick

// One transparent overlay over the whole screen. At rest it only takes
// touches in thin strips along the right, bottom and top edges. A finger that
// lands there drags the window beside that edge smaller, or the scratchpad
// console down, and the room it makes becomes the launcher. Pictures of the
// windows move under the finger; Hyprland only lays out the real ones once
// the finger has decided.
Window {
    id: win

    title: "stage"
    color: "transparent"
    visible: false

    Style { id: style }

    // How far in from the edge a finger has to land to start a gesture.
    readonly property real strip: 10
    readonly property real gap: 8

    // What the gesture acts on (from stage.beginEdge) and how far it has got.
    property var plan: null
    property string phase: "idle"   // idle, tracking, settling, room, launching, leaving, dismiss
    property real pull: 0
    property real fraction: 1       // the share the shrinking window keeps once settled
    property real veil: 1
    property point start
    property var samples: []
    property var ready: ({})        // snapshot ids that have arrived
    property string launchingIcon: ""

    readonly property string edge: plan ? plan.edge : ""
    readonly property bool sideways: edge === "right"
    readonly property bool splitting: (edge === "right" || edge === "bottom") && plan !== null && !plan.empty
    readonly property bool fresh: (edge === "right" || edge === "bottom") && plan !== null && plan.empty === true
    readonly property rect target: splitting ? Qt.rect(plan.x, plan.y, plan.w, plan.h) : Qt.rect(0, 0, 0, 0)
    readonly property rect usable: plan && plan.usable ? Qt.rect(plan.usable.x, plan.usable.y, plan.usable.w, plan.usable.h)
                                                       : Qt.rect(0, 0, width, height)
    readonly property rect box: plan && plan.console ? Qt.rect(plan.console.x, plan.console.y, plan.console.w, plan.console.h)
                                                     : Qt.rect(0, 0, 0, 0)
    // How far the console has come down, 0 to 1.
    readonly property real drop: edge === "top" && box.height > 0 ? Math.min(1, pull / box.height) : 0

    // At rest only the edges (and, while the console is open, the dimmed
    // screen around it) take touches; during a gesture everything does.
    readonly property var inputRects: {
        if (phase !== "idle")
            return [Qt.rect(0, 0, width, height)]
        const rects = [Qt.rect(width - strip, 0, strip, height), Qt.rect(0, height - strip, width, strip),
                       Qt.rect(0, 0, width, strip - 2)]
        if (stage.consoleOpen) {
            const c = stage.consoleRect
            rects.push(Qt.rect(0, c.y + c.height, width, height - c.y - c.height))
            rects.push(Qt.rect(0, c.y, c.x, c.height))
            rects.push(Qt.rect(c.x + c.width, c.y, width - c.x - c.width, c.height))
        }
        return rects
    }
    onInputRectsChanged: stage.setInputRegion(inputRects)

    function iconFor(appClass) {
        return apps.icon(appClass) || (appClass || "").toLowerCase()
    }

    function inside(r, x, y) {
        return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height
    }

    function edgeAt(x, y) {
        if (x >= width - strip * 1.5)
            return "right"
        if (y >= height - strip * 1.5)
            return "bottom"
        if (stage.consoleOpen && !inside(stage.consoleRect, x, y))
            return "dismiss"
        if (y <= strip * 1.5)
            return "top"
        return ""
    }

    // ---- the finger ----

    function pressed(p) {
        if (phase === "room") {
            // A tap beside the launcher puts things back.
            if (!inside(Qt.rect(room.x, room.y, room.width, room.height), p.x, p.y)
                    && !inside(Qt.rect(dropped.x, dropped.y, dropped.width, dropped.height), p.x, p.y))
                cancel()
            return
        }
        if (phase !== "idle")
            return
        const at = edgeAt(p.x, p.y)
        if (at === "")
            return
        start = Qt.point(p.x, p.y)
        samples = []
        if (at === "dismiss") {
            phase = "dismiss"
            return
        }
        plan = stage.beginEdge(at, p.x, p.y)
        ready = ({})
        pull = 0
        fraction = 1
        veil = 1
        phase = "tracking"
    }

    function distance(p) {
        return edge === "right" ? start.x - p.x : edge === "bottom" ? start.y - p.y : p.y - start.y
    }

    // Past what's allowed the picture still gives a little, the way iOS
    // lets you feel the end of a list.
    function rubber(d) {
        return d <= 0 ? 0 : 36 * (1 - Math.exp(-d / 140))
    }

    function moved(p) {
        if (phase !== "tracking")
            return
        const d = distance(p)
        const now = Date.now()
        samples.push({d: d, t: now})
        while (samples.length > 2 && now - samples[0].t > 90)
            samples.shift()
        if (!plan.allowed) {
            pull = rubber(d)
        } else if (splitting) {
            const size = sideways ? target.width : target.height
            const most = size * 0.75
            pull = d <= most ? Math.max(0, d) : most + rubber(d - most)
        } else if (edge === "top") {
            pull = d <= box.height ? Math.max(0, d) : box.height + rubber(d - box.height)
        } else {
            pull = Math.max(0, d)
        }
    }

    // How fast the finger was going as it lifted, in px/s, toward the room.
    function speed() {
        if (samples.length < 2)
            return 0
        const a = samples[0], b = samples[samples.length - 1]
        return (b.d - a.d) / Math.max(1, b.t - a.t) * 1000
    }

    function released(p) {
        if (phase === "dismiss") {
            phase = "idle"
            // A tap, or a push upward, on the dimmed screen sends the console back.
            if (Math.hypot(p.x - start.x, p.y - start.y) < 14 || start.y - p.y > 40)
                stage.closeConsole()
            return
        }
        if (phase !== "tracking")
            return
        const v = speed()
        if (!plan.allowed) {
            settle(0, finish)
            return
        }
        if (edge === "top") {
            if (pull > box.height * 0.35 || v > 700) {
                settle(box.height, function () {
                    if (plan.empty) {
                        phase = "room"
                    } else {
                        phase = "leaving"
                        stage.openConsole()
                        leaveTimer.start()
                    }
                })
            } else {
                settle(0, finish)
            }
            return
        }
        if (fresh) {
            if (pull > 48 || v > 500)
                settle(220, function () { phase = "room" })
            else
                settle(0, finish)
            return
        }
        const size = sideways ? target.width : target.height
        if (pull < 56 && v < 450) {
            settle(0, finish)
            return
        }
        // Let go somewhere: the nearest of a third, a half and two thirds;
        // a short, quick flick makes even halves.
        const kept = (size - pull) / size
        let choice = [2 / 3, 1 / 2, 1 / 3].reduce((a, b) => Math.abs(b - kept) < Math.abs(a - kept) ? b : a)
        if (v > 900 && pull < size * 0.25)
            choice = 1 / 2
        fraction = choice
        settle(size * (1 - choice), function () { phase = "room" })
    }

    function cancel() {
        settle(0, finish)
    }

    function finish() {
        phase = "idle"
        plan = null
        pull = 0
        fraction = 1
        veil = 1
        launchingIcon = ""
    }

    function launchApp(appId) {
        launchingIcon = apps.icon(appId)
        phase = "launching"
        stage.launch(plan, appId, fraction)
    }

    property var afterSettle: null
    function settle(to, done) {
        phase = "settling"
        afterSettle = done
        settler.to = to
        settler.restart()
    }

    SequentialAnimation {
        id: settler
        property real to: 0
        NumberAnimation {
            target: win
            property: "pull"
            to: settler.to
            duration: 320
            // A little overshoot when it lands somewhere, none on the way back.
            easing.type: settler.to > 0 ? Easing.OutBack : Easing.OutCubic
            easing.overshoot: 0.7
        }
        ScriptAction {
            script: {
                const done = win.afterSettle
                win.afterSettle = null
                if (done)
                    done()
            }
        }
    }

    SequentialAnimation {
        id: fadeOut
        PauseAnimation { duration: 120 }
        NumberAnimation { target: win; property: "veil"; to: 0; duration: 200; easing.type: Easing.OutCubic }
        ScriptAction { script: win.finish() }
    }

    // The console is down once Hyprland says so, or after a moment anyway.
    Timer {
        id: leaveTimer
        interval: 800
        onTriggered: if (win.phase === "leaving") fadeOut.start()
    }

    Connections {
        target: stage
        function onSnapshotReady(id, ok) {
            if (!ok)
                return
            const r = Object.assign({}, win.ready)
            r[id] = true
            win.ready = r
        }
        function onPlaced(ok) {
            if (win.phase === "launching")
                fadeOut.start()
        }
        function onConsoleOpenChanged() {
            if (win.phase === "leaving" && stage.consoleOpen) {
                leaveTimer.stop()
                fadeOut.start()
            }
        }
    }

    // ---- what the gesture shows ----

    Item {
        id: scene
        anchors.fill: parent
        opacity: win.veil
        visible: win.phase !== "idle" && win.phase !== "dismiss"

        // The window being made smaller. The real one stays where it is: the
        // room drawn over its far side is what makes it look cut off, so
        // there is nothing to capture or wait for.
        Item {
            id: shrinking
            x: win.target.x
            y: win.target.y
            width: win.sideways ? Math.max(0, win.target.width - win.pull) : win.target.width
            height: win.sideways ? win.target.height : Math.max(0, win.target.height - win.pull)
        }

        // A soft shadow along the edge the finger is pulling.
        Rectangle {
            visible: win.splitting && win.pull > 1
            x: win.sideways ? shrinking.x + shrinking.width - width : shrinking.x
            y: win.sideways ? shrinking.y : shrinking.y + shrinking.height - height
            width: win.sideways ? 14 : shrinking.width
            height: win.sideways ? shrinking.height : 14
            gradient: Gradient {
                orientation: win.sideways ? Gradient.Horizontal : Gradient.Vertical
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: style.shadow }
            }
        }

        // The room the finger made, and the launcher in it.
        Rectangle {
            id: roomBack
            visible: win.splitting
            color: style.background
            x: win.sideways ? shrinking.x + shrinking.width : win.target.x
            y: win.sideways ? win.target.y : shrinking.y + shrinking.height
            width: win.sideways ? Math.max(0, win.target.width - shrinking.width) : win.target.width
            height: win.sideways ? win.target.height : Math.max(0, win.target.height - shrinking.height)
        }

        Item {
            id: room
            visible: win.splitting || win.fresh
            x: win.fresh ? win.usable.x + 12 : roomBack.x + (win.sideways ? win.gap : 0)
            y: win.fresh ? win.usable.y + 12 : roomBack.y + (win.sideways ? 0 : win.gap)
            width: win.fresh ? win.usable.width - 24 : Math.max(0, roomBack.width - (win.sideways ? win.gap : 0))
            height: win.fresh ? win.usable.height - 24 : Math.max(0, roomBack.height - (win.sideways ? 0 : win.gap))
            opacity: win.fresh ? Math.min(1, win.pull / 220) : Math.min(1, win.pull / 140)
            scale: win.fresh ? 0.94 + 0.06 * Math.min(1, win.pull / 220) : 1
            clip: true

            Launcher {
                anchors.fill: parent
                style: style
                interactive: win.phase === "room"
                opacity: win.phase === "launching" ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 150 } }
                onPicked: (appId) => win.launchApp(appId)
            }
        }

        // The console, pulled down from the top over a darkening screen.
        Rectangle {
            visible: win.edge === "top"
            anchors.fill: parent
            color: "black"
            opacity: 0.6 * win.drop
        }

        Item {
            id: dropped
            visible: win.edge === "top"
            x: win.box.x
            width: win.box.width
            height: win.box.height
            y: win.box.y - (win.box.y + win.box.height) * (1 - win.drop)
               + (win.pull > win.box.height ? win.pull - win.box.height : 0)

            Repeater {
                model: win.edge === "top" && win.plan.windows ? win.plan.windows : []
                delegate: Item {
                    required property var modelData
                    x: modelData.x - win.box.x
                    y: modelData.y - win.box.y
                    width: modelData.w
                    height: modelData.h

                    Rectangle {
                        anchors.fill: parent
                        color: style.panel
                        Image {
                            anchors.centerIn: parent
                            width: 80
                            height: 80
                            sourceSize: Qt.size(160, 160)
                            source: "image://icon/" + win.iconFor(parent.parent.modelData.appClass)
                            asynchronous: true
                        }
                    }
                    Image {
                        anchors.fill: parent
                        visible: win.ready[parent.modelData.snapshot] === true
                        source: visible ? "image://snapshot/" + parent.modelData.snapshot : ""
                        cache: false
                    }
                }
            }

            Launcher {
                anchors.fill: parent
                visible: win.edge === "top" && win.plan.empty === true
                style: style
                interactive: win.phase === "room"
                opacity: win.phase === "launching" ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 150 } }
                onPicked: (appId) => win.launchApp(appId)
            }
        }

        // The app on its way, where it will appear.
        Image {
            readonly property Item host: win.edge === "top" ? dropped : room
            visible: win.phase === "launching" && win.launchingIcon !== ""
            width: 112
            height: 112
            x: host.x + host.width / 2 - width / 2
            y: host.y + host.height / 2 - height / 2
            sourceSize: Qt.size(224, 224)
            source: win.launchingIcon !== "" ? "image://icon/" + win.launchingIcon : ""
            SequentialAnimation on scale {
                running: win.phase === "launching"
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.9; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.9; to: 1; duration: 520; easing.type: Easing.InOutSine }
            }
        }
    }

    // A finger without a hand, for `stage simulate`: drags from one point to
    // another over `ms` and keeps holding, so the screen can be looked at
    // mid-gesture, then lets go or picks an app.
    function simulate(action, x0, y0, x1, y1, ms) {
        if (action === "drag") {
            fakeFinger.from = Qt.point(x0, y0)
            fakeFinger.to = Qt.point(x1, y1)
            fakeFinger.steps = Math.max(1, Math.round(ms / fakeFinger.interval))
            fakeFinger.step = 0
            pressed({x: x0, y: y0})
            fakeFinger.restart()
        } else if (action === "release") {
            fakeFinger.stop()
            released({x: fakeFinger.at.x, y: fakeFinger.at.y})
        } else if (action === "pick" && phase === "room") {
            launchApp(x0)
        } else if (action === "cancel") {
            fakeFinger.stop()
            cancel()
        }
        return phase
    }

    Timer {
        id: fakeFinger
        property point from
        property point to
        property point at
        property int steps: 1
        property int step: 0
        interval: 16
        repeat: true
        onTriggered: {
            step += 1
            const t = Math.min(1, step / steps)
            at = Qt.point(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t)
            win.moved({x: at.x, y: at.y})
            if (t >= 1)
                stop()
        }
    }

    // Every app's icon, loaded once at the size the launcher asks for, so the
    // first gesture doesn't show a launcher of names waiting for pictures.
    Repeater {
        model: apps
        delegate: Image {
            required property string icon
            visible: false
            sourceSize: Qt.size(128, 128)
            source: "image://icon/" + icon
            asynchronous: true
        }
    }

    MultiPointTouchArea {
        anchors.fill: parent
        z: -1
        maximumTouchPoints: 1
        mouseEnabled: true
        onPressed: (touchPoints) => win.pressed(touchPoints[0])
        onUpdated: (touchPoints) => win.moved(touchPoints[0])
        onReleased: (touchPoints) => win.released(touchPoints[0])
        onCanceled: (touchPoints) => { if (win.phase === "tracking") win.cancel() }
    }
}
