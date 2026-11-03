import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic

// The owner setup by touch. Omarchy's own setup asks the questions (on tty1, through the stand-in gum); this window
// shows each one as a page, checks the answer against Omarchy's own rules, and sends it back. Between questions it
// shows the welcome page, a moment's wait, or the setup's progress.
Window {
    id: root

    visible: true
    visibility: windowed ? Window.Windowed : Window.FullScreen
    width: 1280
    height: 900
    color: colors.base
    title: "Omarchy setup"

    // Omarchy's Tokyo Night, the colours its text form sets on the console.
    QtObject {
        id: colors
        readonly property color base: "#1a1b26"
        readonly property color raised: "#24283b"
        readonly property color line: "#414868"
        readonly property color text: "#c0caf5"
        readonly property color muted: "#a9b1d6"
        readonly property color faint: "#565f89"
        readonly property color accent: "#7aa2f7"
        readonly property color good: "#9ece6a"
        readonly property color bad: "#f7768e"
    }

    // One unit per 900 px of the short side: the 2400 px Pad and a test window get the same layout.
    readonly property real s: Math.max(0.8, Math.min(width, height) / 900)
    readonly property string fontFamily: "Adwaita Sans"

    // The field the on-screen keyboard types into, and whether it is up.
    property var target: null
    property bool keyboardOpen: false
    readonly property real keyboardHeight: Math.round(0.32 * Math.min(width, height))

    // A confirmation typed on the password page, answered for Omarchy's second password question.
    property string pendingConfirmation: ""
    property bool hasPendingConfirmation: false

    property var layoutCodes: ["us"]

    readonly property var q: form.question
    readonly property string cmd: form.asked ? q.cmd : ""

    function stripPrompt(p) {
        return (p || "").replace(/>\s*$/, "").trim()
    }

    function isPrompt(p, key) {
        return (p || "") === form.text(key)
    }

    Connections {
        target: form
        function onQuestionChanged() {
            const xkb = form.xkbLayout()
            root.layoutCodes = xkb && xkb !== "us" ? [xkb, "us"] : ["us"]
            // Omarchy's second password question: already typed on the password page.
            if (form.asked && root.cmd === "input" && root.isPrompt(root.q.prompt, "setupForm.confirmPrompt")
                && root.hasPendingConfirmation) {
                const confirmation = root.pendingConfirmation
                root.pendingConfirmation = ""
                root.hasPendingConfirmation = false
                form.answer(0, confirmation)
            }
        }
    }

    // The window's colour, as an item: what tests/check pictures is the content, without the window behind it.
    Rectangle { anchors.fill: parent; color: colors.base }

    // ---- the page area, above the keyboard -------------------------------------------------------------------

    Item {
        id: area
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: keyboard.visible ? keyboard.height * keyboard.reveal : 0

        // Omarchy's logo is drawn in black; it shows in the console's green, as the text form prints it.
        Image {
            id: logo
            source: form.tinted(omarchyPath + "/logo.svg", colors.good)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 40 * s
            height: (keyboard.reveal > 0.5 ? 36 : 64) * s
            fillMode: Image.PreserveAspectFit
            sourceSize.height: 256
            smooth: true
            Behavior on height { NumberAnimation { duration: 200 } }
        }

        // The power button, on question pages: Omarchy's way out (Ctrl+C), which asks whether to reboot.
        RoundButton {
            visible: root.cmd === "input" || root.cmd === "choose" || root.cmd === "filter"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24 * s
            width: 44 * s
            height: width
            onClicked: form.answer(130)
            contentItem: Text {
                text: "⏻"
                color: colors.faint
                font.pixelSize: 22 * s
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: width / 2; color: parent.down ? colors.raised : "transparent" }
        }

        Loader {
            id: page
            anchors.top: logo.bottom
            anchors.topMargin: 28 * s
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24 * s
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(720 * s, parent.width - 48 * s)
            sourceComponent: !form.started ? welcomePage
                : root.cmd === "choose" || root.cmd === "filter" ? listPage
                : root.cmd === "input" ? (root.isPrompt(root.q.prompt, "setupForm.passwordPrompt") ? passwordPage : inputPage)
                : root.cmd === "confirm" ? confirmPage
                : form.setupState !== "" ? progressPage
                : waitPage
        }
    }

    // ---- building blocks -------------------------------------------------------------------------------------

    component Title: Text {
        color: colors.text
        font.family: root.fontFamily
        font.pixelSize: 30 * s
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
        width: parent ? parent.width : 0
    }

    component Hint: Text {
        color: colors.muted
        font.family: root.fontFamily
        font.pixelSize: 17 * s
        wrapMode: Text.WordWrap
        width: parent ? parent.width : 0
    }

    component Notice: Text {
        color: colors.bad
        font.family: root.fontFamily
        font.pixelSize: 17 * s
        wrapMode: Text.WordWrap
        width: parent ? parent.width : 0
        visible: text !== ""
    }

    // A turning arc in the accent colour (the Basic style's BusyIndicator is drawn for light backgrounds).
    component Spinner: Item {
        width: 30 * s
        height: width
        property bool running: true
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = Math.max(2, width / 9)
                ctx.lineCap = "round"
                ctx.strokeStyle = colors.accent
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - ctx.lineWidth, 0, Math.PI * 1.5)
                ctx.stroke()
            }
            RotationAnimator on rotation {
                running: parent.running && parent.visible
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
            }
        }
    }

    component PadButton: Button {
        id: button
        property bool primary: true
        height: 56 * s
        font.family: root.fontFamily
        font.pixelSize: 19 * s
        font.weight: Font.Medium
        contentItem: Text {
            text: button.text
            font: button.font
            color: button.primary ? colors.base : colors.text
            opacity: button.enabled ? 1 : 0.5
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 12 * s
            color: button.primary ? (button.down ? Qt.darker(colors.accent, 1.2) : colors.accent)
                                  : (button.down ? colors.line : colors.raised)
        }
    }

    component Field: TextField {
        id: field
        width: parent ? parent.width : 0
        height: 60 * s
        color: colors.text
        placeholderTextColor: colors.faint
        font.family: root.fontFamily
        font.pixelSize: 20 * s
        leftPadding: 18 * s
        rightPadding: 18 * s
        selectionColor: colors.accent
        background: Rectangle {
            radius: 12 * s
            color: colors.raised
            border.width: field.activeFocus ? 2 * s : 1
            border.color: field.activeFocus ? colors.accent : colors.line
        }
        onActiveFocusChanged: if (activeFocus) { root.target = field; root.keyboardOpen = true }
        TapHandler { onTapped: { field.forceActiveFocus(); root.keyboardOpen = true } }
    }

    // Back (Omarchy's Esc) and the page's own button.
    component Buttons: Row {
        id: buttons
        property string label: "Continue"
        property bool canGo: true
        property bool canBack: true
        signal go()
        width: parent ? parent.width : 0
        spacing: 14 * s
        layoutDirection: Qt.RightToLeft
        PadButton { text: buttons.label; enabled: buttons.canGo; width: 200 * s; onClicked: buttons.go() }
        PadButton { text: "Back"; primary: false; visible: buttons.canBack; width: 140 * s; onClicked: form.answer(1) }
    }

    // ---- pages -----------------------------------------------------------------------------------------------

    Component {
        id: welcomePage
        Column {
            spacing: 22 * s
            property bool tapped: false
            Item { width: 1; height: 40 * s }
            Title { text: "Welcome to Omarchy"; font.pixelSize: 40 * s; horizontalAlignment: Text.AlignHCenter }
            Hint {
                text: "Let's set up this tablet: your keyboard layout, your account and your timezone."
                horizontalAlignment: Text.AlignHCenter
            }
            Item { width: 1; height: 20 * s }
            PadButton {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 260 * s
                text: parent.tapped ? "Starting…" : "Start setup"
                enabled: !parent.tapped
                onClicked: { parent.tapped = true; form.start() }
            }
        }
    }

    Component {
        id: waitPage
        Item {
            Spinner { anchors.centerIn: parent; width: 56 * s }
        }
    }

    // gum choose and gum filter: a list to pick one from, searchable when it is long (the timezones).
    Component {
        id: listPage
        Item {
            id: list
            readonly property var options: root.q.options || []
            readonly property bool searchable: root.cmd === "filter" || options.length > 12
            property string chosen: root.q.selected || ""
            property string filter: ""
            readonly property var shown: filter === "" ? options
                : options.filter(o => o.toLowerCase().indexOf(filter.toLowerCase()) !== -1)

            Column {
                id: head
                width: parent.width
                spacing: 14 * s
                Title { text: root.q.header || "Choose one" }
                Notice { text: form.notice }
                Field {
                    visible: list.searchable
                    placeholderText: root.q.placeholder || "Search"
                    onTextChanged: list.filter = text
                }
            }

            ListView {
                id: view
                anchors.top: head.bottom
                anchors.topMargin: 14 * s
                anchors.bottom: buttons.top
                anchors.bottomMargin: 18 * s
                width: parent.width
                clip: true
                model: list.shown
                spacing: 4 * s
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property string modelData
                    width: view.width
                    height: 54 * s
                    radius: 10 * s
                    color: modelData === list.chosen ? Qt.rgba(0.48, 0.64, 0.97, 0.18)
                         : tap.pressed ? colors.raised : "transparent"
                    border.width: modelData === list.chosen ? 2 * s : 0
                    border.color: colors.accent
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 18 * s
                        width: parent.width - 36 * s
                        text: modelData
                        elide: Text.ElideRight
                        color: colors.text
                        font.family: root.fontFamily
                        font.pixelSize: 19 * s
                    }
                    TapHandler { id: tap; onTapped: { list.chosen = modelData; root.keyboardOpen = false } }
                }
                Component.onCompleted: {
                    const i = list.options.indexOf(list.chosen)
                    if (i >= 0) positionViewAtIndex(i, ListView.Center)
                }
            }

            Buttons {
                id: buttons
                anchors.bottom: parent.bottom
                canGo: list.chosen !== "" && list.shown.indexOf(list.chosen) !== -1
                onGo: form.answer(0, list.chosen)
            }
        }
    }

    // gum input: one field, checked against Omarchy's rules where it has them (the user name and the hostname).
    Component {
        id: inputPage
        Column {
            id: input
            spacing: 16 * s
            readonly property bool isUsername: root.isPrompt(root.q.prompt, "setupForm.usernamePrompt")
            readonly property bool isHostname: root.isPrompt(root.q.prompt, "setupForm.hostnamePrompt")
            property string error: form.notice

            function check(value) {
                const rules = form.rules
                if (isUsername) {
                    if (!new RegExp(rules.username).test(value)) return form.text("setupForm.usernameInvalid")
                    if (new RegExp(rules.reserved).test(value)) return form.text("setupForm.usernameReserved")
                    if (form.userExists(value)) return form.text("setupForm.usernameTaken")
                }
                if (isHostname && value !== "" && !new RegExp(rules.hostname).test(value))
                    return form.text("setupForm.hostnameInvalid")
                return ""
            }

            function submit() {
                error = check(field.text)
                if (error === "") form.answer(0, field.text)
            }

            Title { text: input.isUsername ? "Your account" : input.isHostname ? "This tablet's name" : root.stripPrompt(root.q.prompt) }
            Hint { text: root.q.placeholder || "" }
            Notice { text: input.error }
            Field {
                id: field
                placeholderText: root.stripPrompt(root.q.prompt)
                echoMode: root.q.password ? TextInput.Password : TextInput.Normal
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: input.submit()
                Component.onCompleted: forceActiveFocus()
            }
            Buttons { onGo: input.submit() }
        }
    }

    // Omarchy asks for the password twice, one question after the other; here both fields are on one page, and the
    // second question gets the confirmation without showing a page.
    Component {
        id: passwordPage
        Column {
            id: password
            spacing: 16 * s
            property string error: form.notice

            function submit() {
                if (first.text === "") error = form.text("setupForm.passwordBlank")
                else if (first.text !== second.text) error = form.text("setupForm.passwordMismatch")
                else {
                    root.pendingConfirmation = second.text
                    root.hasPendingConfirmation = true
                    form.answer(0, first.text)
                }
            }

            Title { text: "Your password" }
            Hint { text: root.q.placeholder || "" }
            Notice { text: password.error }
            Field {
                id: first
                placeholderText: "Password"
                echoMode: TextInput.Password
                onAccepted: second.forceActiveFocus()
                Component.onCompleted: forceActiveFocus()
            }
            Field {
                id: second
                placeholderText: "The same password again"
                echoMode: TextInput.Password
                onAccepted: password.submit()
            }
            Buttons { onGo: password.submit() }
        }
    }

    // gum confirm, with the summary card when gum table came just before it.
    Component {
        id: confirmPage
        Column {
            spacing: 22 * s
            Title { text: root.q.prompt || "Continue?" }
            Rectangle {
                visible: form.summary.length > 1
                width: parent.width
                height: rows.height + 28 * s
                radius: 14 * s
                color: colors.raised
                Column {
                    id: rows
                    x: 22 * s
                    y: 14 * s
                    width: parent.width - 44 * s
                    Repeater {
                        model: form.summary.slice(1)
                        Item {
                            required property var modelData
                            width: rows.width
                            height: 48 * s
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData[0] || ""
                                color: colors.muted
                                font.family: root.fontFamily
                                font.pixelSize: 18 * s
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                text: modelData[1] || ""
                                color: colors.text
                                font.family: root.fontFamily
                                font.pixelSize: 18 * s
                                font.weight: Font.Medium
                            }
                        }
                    }
                }
            }
            Row {
                width: parent.width
                spacing: 14 * s
                layoutDirection: Qt.RightToLeft
                PadButton { text: root.q.affirmative || "Yes"; width: 220 * s; onClicked: form.answer(0) }
                PadButton { text: root.q.negative || "No"; primary: false; width: 220 * s; onClicked: form.answer(1) }
            }
        }
    }

    // After the summary: Omarchy creates the account, sets up the desktop and the boot menu. Its state file names the
    // step; SDDM takes the screen when it is done.
    Component {
        id: progressPage
        Column {
            spacing: 18 * s
            readonly property var steps: [
                {state: "account", label: "Creating your account"},
                {state: "finalize", label: "Setting up your desktop"},
                {state: "rekey", label: "Securing the disk"},
                {state: "boot", label: "Updating the boot menu"},
                {state: "done", label: "Starting Omarchy"}]
            readonly property int current: steps.findIndex(step => step.state === form.setupState)

            Title { text: "Setting up your tablet" }
            Hint { text: "This takes a minute or two." }
            Item { width: 1; height: 8 * s }
            Repeater {
                model: parent.steps
                Row {
                    required property var modelData
                    required property int index
                    readonly property int current: parent.current
                    // Securing the disk only happens on an encrypted tablet.
                    visible: modelData.state !== "rekey" || form.setupState === "rekey"
                    spacing: 16 * s
                    Item {
                        width: 30 * s
                        height: 30 * s
                        Spinner {
                            anchors.fill: parent
                            running: index === current && modelData.state !== "done"
                            visible: running
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: index < current || (index === current && modelData.state === "done")
                            text: "✓"
                            color: colors.good
                            font.pixelSize: 24 * s
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: index <= current ? colors.text : colors.faint
                        font.family: root.fontFamily
                        font.pixelSize: 20 * s
                    }
                }
            }
        }
    }

    // ---- tests/check: each page pictured, then answered with test values --------------------------------------

    property int shot: 0
    Timer {
        id: autoAnswer
        interval: 400
        running: autotest !== "" && (form.asked || !form.started)
        repeat: true
        onTriggered: {
            const name = !form.started ? "welcome" : root.cmd
            const file = autotest + "/" + String(++root.shot).padStart(2, "0") + "-" + name + ".png"
            const answered = form.asked ? form.serial : -1
            root.contentItem.grabToImage(function(result) {
                result.saveToFile(file)
                if (answered < 0) { form.start(); return }
                if (!form.asked || form.serial !== answered) return
                if (root.cmd === "choose" || root.cmd === "filter") form.answer(0, root.q.selected || root.q.options[0])
                else if (root.cmd === "confirm") form.answer(0)
                else if (root.isPrompt(root.q.prompt, "setupForm.usernamePrompt")) form.answer(0, "owner")
                else if (root.isPrompt(root.q.prompt, "setupForm.passwordPrompt")) {
                    root.pendingConfirmation = "test"
                    root.hasPendingConfirmation = true
                    form.answer(0, "test")
                } else if (root.isPrompt(root.q.prompt, "setupForm.hostnamePrompt")) form.answer(0, "pad")
                else form.answer(0, "x")
            })
        }
    }
    Connections {
        target: autotest !== "" ? form : null
        function onSetupStateChanged() {
            const file = autotest + "/" + String(++root.shot).padStart(2, "0") + "-progress-" + form.setupState + ".png"
            Qt.callLater(() => root.contentItem.grabToImage(result => result.saveToFile(file)))
        }
    }

    // ---- floatkey's keys, the lock screen's keyboard ---------------------------------------------------------

    LockKeyboard {
        id: keyboard

        // Up while a field of the page on screen has the focus: a new page brings its own field (or none).
        property real reveal: root.keyboardOpen && root.target
            && (root.cmd === "input" || root.cmd === "choose" || root.cmd === "filter") ? 1 : 0
        Behavior on reveal { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

        anchors.left: parent.left
        anchors.right: parent.right
        height: root.keyboardHeight
        y: root.height - height * reveal
        visible: reveal > 0

        background: colors.base
        foreground: colors.text
        accent: colors.accent
        radius: 10 * s
        layoutCodes: root.layoutCodes

        onTyped: function(text) {
            if (!root.target) return
            if (root.target.selectedText !== "") root.target.remove(root.target.selectionStart, root.target.selectionEnd)
            root.target.insert(root.target.cursorPosition, text)
        }
        onErased: {
            if (!root.target) return
            if (root.target.selectedText !== "") root.target.remove(root.target.selectionStart, root.target.selectionEnd)
            else if (root.target.cursorPosition > 0)
                root.target.remove(root.target.cursorPosition - 1, root.target.cursorPosition)
        }
        onSubmitted: if (root.target) root.target.accepted()
        onHideRequested: root.keyboardOpen = false
    }
}
