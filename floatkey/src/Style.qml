import QtQuick

// Key colors mixed from the Omarchy theme, the way an iPad keyboard layers
// them: letter keys a step lighter than the board, special keys a step darker,
// the accent for anything that is switched on.
QtObject {
    readonly property bool dark: theme.dark
    readonly property color background: theme.background
    readonly property color foreground: theme.foreground
    readonly property color accent: theme.accent
    readonly property color accentText: theme.accentForeground

    readonly property color black: Qt.rgba(0, 0, 0, 1)
    readonly property color white: Qt.rgba(1, 1, 1, 1)

    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
    }
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    readonly property color board: dark ? mix(background, black, 0.3) : mix(background, black, 0.1)
    readonly property color edge: dark ? alpha(mix(background, foreground, 0.35), 0.6) : alpha(black, 0.12)
    readonly property color key: dark ? mix(background, foreground, 0.27) : mix(background, white, 0.9)
    readonly property color keyDown: dark ? mix(background, foreground, 0.12) : mix(background, black, 0.2)
    readonly property color special: dark ? mix(background, foreground, 0.12) : mix(background, black, 0.17)
    readonly property color specialDown: key
    readonly property color label: dark ? mix(foreground, white, 0.5) : mix(foreground, black, 0.4)
    readonly property color faint: alpha(label, 0.5)
    readonly property color shadow: dark ? alpha(black, 0.55) : alpha(black, 0.28)
    readonly property color glow: alpha(accent, 0.22)

    readonly property string font: "Adwaita Sans"
    readonly property string emojiFont: "Noto Color Emoji"
    // Rounded like an iPad's keys, whatever Hyprland does with window corners.
    readonly property real keyRadius: Math.max(6, Math.min(theme.radius, 10))
    readonly property real boardRadius: Math.max(14, Math.min(theme.radius * 1.5, 22))
}
