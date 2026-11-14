import QtQuick
import QtQuick.Window
import Camera

Window {
    id: window
    visible: true
    width: 1200
    height: 800
    color: "black"
    title: qsTr("Camera")

    // Landscape puts the controls in a column along the right edge, under the
    // thumb; portrait puts them in a row along the bottom.
    readonly property bool landscape: width >= height
    readonly property real unit: Math.max(48, Math.min(width, height) / 14)

    // The screen's clockwise turn from the panel's native orientation, which
    // the frame turns back from.
    Binding {
        target: cameras
        property: "screenRotation"
        value: (360 - Screen.angleBetween(Screen.nativeOrientation, Screen.orientation)) % 360
    }

    Viewfinder {
        id: viewfinder
        objectName: "viewfinder"
        anchors.fill: parent
        source: cameras
        opacity: frozen ? 0.5 : 1
        Behavior on opacity { NumberAnimation { duration: 150 } }

        TapHandler {
            onTapped: (point) => {
                if (!cameras.canFocus || cameras.state !== "running")
                    return
                const p = viewfinder.framePoint(point.position.x, point.position.y)
                if (p.x < 0)
                    return
                focusSquare.x = point.position.x - focusSquare.width / 2
                focusSquare.y = point.position.y - focusSquare.height / 2
                focusSquare.shown = true
                cameras.focusAt(p.x, p.y)
            }
        }
    }

    // Where the tap focuses: pulses while the lens moves, then fades.
    Rectangle {
        id: focusSquare
        property bool shown: false
        width: window.unit * 1.6
        height: width
        color: "transparent"
        border.color: theme.accent
        border.width: 2
        radius: 4
        visible: opacity > 0
        opacity: shown ? 1 : 0
        scale: cameras.focusing ? 0.85 : 1
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Behavior on scale { NumberAnimation { duration: 250; easing.type: Easing.OutBack } }
        Timer {
            running: focusSquare.shown && !cameras.focusing
            interval: 1200
            onTriggered: focusSquare.shown = false
        }
    }

    // A white flash the moment the shutter is pressed.
    Rectangle {
        id: flash
        anchors.fill: parent
        color: "white"
        opacity: 0
        NumberAnimation on opacity {
            id: flashAnimation
            running: false
            from: 0.8
            to: 0
            duration: 220
        }
    }

    Text {
        objectName: "message"
        anchors.centerIn: parent
        width: parent.width * 0.7
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        font.pixelSize: window.unit * 0.4
        visible: text !== ""
        text: cameras.state === "none" ? qsTr("No camera found")
            : cameras.state === "busy" ? qsTr("Another app is using the camera.\nTap to try again.")
            : ""
        TapHandler {
            enabled: cameras.state === "busy"
            onTapped: cameras.start()
        }
    }

    // The controls: the last photo, the shutter, the other camera.
    Item {
        id: controls
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: window.landscape ? window.unit * 2.4 : parent.width
        height: window.landscape ? parent.height : window.unit * 2.4

        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: 0.35
        }

        Grid {
            anchors.centerIn: parent
            columns: window.landscape ? 1 : 3
            rows: window.landscape ? 3 : 1
            spacing: window.unit * 1.4
            verticalItemAlignment: Grid.AlignVCenter
            horizontalItemAlignment: Grid.AlignHCenter
            // The last photo comes first in portrait (left) and last in
            // landscape (bottom), as on an iPad.

            Loader { sourceComponent: window.landscape ? switchButton : lastPhoto }

            Item {
                id: shutter
                objectName: "shutter"
                width: window.unit * 1.5
                height: width
                readonly property bool ready: cameras.state === "running"
                opacity: ready ? 1 : 0.4

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.color: "white"
                    border.width: Math.max(3, width / 16)
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * (shutterTap.pressed ? 0.72 : 0.8)
                    height: width
                    radius: width / 2
                    color: "white"
                    Behavior on width { NumberAnimation { duration: 80 } }
                }
                TapHandler {
                    id: shutterTap
                    enabled: shutter.ready
                    onTapped: {
                        flashAnimation.restart()
                        cameras.takePhoto()
                    }
                }
            }

            Loader { sourceComponent: window.landscape ? lastPhoto : switchButton }
        }
    }

    Component {
        id: lastPhoto
        Rectangle {
            objectName: "lastPhoto"
            width: window.unit * 1.1
            height: width
            radius: 8
            color: "#333"
            border.color: "white"
            border.width: 2
            clip: true
            // Hidden but keeping its place, so the shutter stays centred.
            readonly property bool shown: cameras.lastPhoto.toString() !== "" || cameras.saving > 0
            opacity: shown ? 1 : 0
            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: cameras.lastPhoto
                sourceSize: Qt.size(256, 256)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                opacity: cameras.saving > 0 ? 0.5 : 1
            }
            TapHandler {
                enabled: parent.shown
                onTapped: cameras.openLastPhoto()
            }
        }
    }

    Component {
        id: switchButton
        Rectangle {
            objectName: "switchCamera"
            width: window.unit * 1.1
            height: width
            radius: width / 2
            color: Qt.rgba(1, 1, 1, switchTap.pressed ? 0.35 : 0.2)
            opacity: cameras.canSwitch ? 1 : 0
            Image {
                anchors.centerIn: parent
                width: parent.width * 0.55
                height: width
                source: "qrc:/switch.svg"
                sourceSize: Qt.size(width * 2, height * 2)
                rotation: cameras.front ? 180 : 0
                Behavior on rotation { NumberAnimation { duration: 250 } }
            }
            TapHandler {
                id: switchTap
                enabled: cameras.canSwitch && (cameras.state === "running" || cameras.state === "busy")
                onTapped: cameras.switchCamera()
            }
        }
    }

    Connections {
        target: cameras
        function onPhotoFailed(why) { failure.text = why; failure.opacity = 1; failureTimer.restart() }
    }

    Text {
        id: failure
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: window.unit
        color: "white"
        font.pixelSize: window.unit * 0.35
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Timer { id: failureTimer; interval: 2500; onTriggered: failure.opacity = 0 }
    }
}
