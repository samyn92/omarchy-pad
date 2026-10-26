import QtQuick

// Colors mixed from the Omarchy theme, like floatkey's: panels a step
// darker than the background, the accent for what is picked.
QtObject {
    readonly property bool dark: theme.dark
    readonly property color background: theme.background
    readonly property color foreground: theme.foreground
    readonly property color accent: theme.accent

    readonly property color black: Qt.rgba(0, 0, 0, 1)
    readonly property color white: Qt.rgba(1, 1, 1, 1)

    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
    }
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    readonly property color panel: dark ? mix(background, black, 0.25) : mix(background, black, 0.06)
    readonly property color tile: dark ? mix(background, foreground, 0.1) : mix(background, white, 0.7)
    readonly property color label: dark ? mix(foreground, white, 0.45) : mix(foreground, black, 0.35)
    readonly property color faint: alpha(label, 0.55)
    readonly property color edge: dark ? alpha(mix(background, foreground, 0.35), 0.5) : alpha(black, 0.12)
    readonly property color shadow: alpha(black, dark ? 0.5 : 0.25)

    readonly property string font: "Adwaita Sans"
    // Rounded like the iPad's panels, whatever Hyprland does with corners.
    readonly property real radius: Math.max(12, Math.min(theme.radius * 1.5, 20))
}
