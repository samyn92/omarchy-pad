import QtQuick

// A window's picture (one of stage's snapshots), upright. One taken on the
// GPU lies the way the compositor copied it, which on a turned screen may be
// on its side; it is turned here as it's drawn, rather than on the CPU.
Item {
    id: picture

    property int snapshot: -1
    // Asked for only once the capture is in: the provider has nothing before.
    property bool shown: true
    // The size it shows at, in pixels: a picture in memory is scaled to it.
    property size sourceSize
    property alias fillMode: image.fillMode
    readonly property alias status: image.status

    // A wl_output_transform: a quarter turn each, 4 and up mirrored too.
    readonly property int turn: shown && snapshot >= 0 ? stage.pictureTurn(snapshot) : 0

    transform: Scale {
        origin.x: picture.width / 2
        xScale: picture.turn >= 4 ? -1 : 1
    }

    Image {
        id: image
        anchors.centerIn: parent
        width: picture.turn % 2 ? picture.height : picture.width
        height: picture.turn % 2 ? picture.width : picture.height
        rotation: -90 * (picture.turn % 4)
        source: picture.shown && picture.snapshot >= 0 ? "image://snapshot/" + picture.snapshot : ""
        sourceSize: picture.turn % 2 ? Qt.size(picture.sourceSize.height, picture.sourceSize.width) : picture.sourceSize
        asynchronous: true
        cache: false
    }
}
