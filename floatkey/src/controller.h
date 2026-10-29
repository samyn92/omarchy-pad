#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class QLocalServer;
class QWindow;
class VirtualKeyboard;
namespace LayerShellQt {
class Window;
}

// The keyboard's state: shown or hidden, where it sits (docked, floating, or
// the voice orb alone), which layout it shows, and the user's settings. It also tells the
// compositor which part of the transparent overlay takes touches, and reserves
// space at the bottom of the screen while the keyboard is docked so windows
// move up instead of hiding behind it. With fcitx5's floatkey addon it comes
// up by itself when a text field takes focus, and goes when it loses it;
// with a touch screen, only when the user tapped into that field.
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QPointF floatingPosition READ floatingPosition WRITE setFloatingPosition NOTIFY floatingPositionChanged)
    // Where the voice orb alone sits, as for floatingPosition.
    Q_PROPERTY(QPointF orbPosition READ orbPosition WRITE setOrbPosition NOTIFY orbPositionChanged)
    Q_PROPERTY(QStringList layouts READ layouts CONSTANT)
    // What XKB says the keys of each layout type, for drawing them
    // (XkbLayouts::read); a layout XKB doesn't know uses floatkey's own table.
    Q_PROPERTY(QVariantMap layoutKeys READ layoutKeys CONSTANT)
    Q_PROPERTY(int layoutIndex READ layoutIndex WRITE setLayoutIndex NOTIFY layoutIndexChanged)
    Q_PROPERTY(int dockedHeight READ dockedHeight WRITE setDockedHeight NOTIFY dockedHeightChanged)
    Q_PROPERTY(double dockedScale READ dockedScale CONSTANT)
    Q_PROPERTY(double compactScale READ compactScale CONSTANT)
    Q_PROPERTY(bool autoCapitalize READ autoCapitalize CONSTANT)
    Q_PROPERTY(bool doubleSpacePeriod READ doubleSpacePeriod CONSTANT)
    Q_PROPERTY(QStringList recentEmoji READ recentEmoji NOTIFY recentEmojiChanged)
    // The kind of field that has focus (text, terminal, url, email, number,
    // password), or empty when none does.
    Q_PROPERTY(QString fieldType READ fieldType NOTIFY fieldTypeChanged)

public:
    // systemLayouts is the compositor's keyboard layout list (Hyprland's
    // input:kb_layout), used until the settings name layouts of their own.
    explicit Controller(const QString &settingsPath, const QStringList &systemLayouts = {},
                        QObject *parent = nullptr);
    ~Controller() override;

    static QStringList modes();
    // The layouts with floatkey's own hand-made tables, used when XKB can't
    // say what a layout's keys type.
    static QStringList supportedLayouts();
    static QString defaultSettingsPath();

    // Sets up the overlay's layer surface; call before the window is shown.
    void attach(QWindow *window, VirtualKeyboard *keyboard);
    bool listen(const QString &socketPath, QString *error);

    // Runs one command line from `floatkey <command>` and returns the reply.
    QString command(const QString &line);

    bool isVisible() const { return m_visible; }
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QPointF floatingPosition() const { return m_floatingPosition; }
    void setFloatingPosition(const QPointF &position);
    QPointF orbPosition() const { return m_orbPosition; }
    void setOrbPosition(const QPointF &position);
    QStringList layouts() const { return m_layouts; }
    QVariantMap layoutKeys() const { return m_layoutKeys; }
    int layoutIndex() const { return m_layoutIndex; }
    void setLayoutIndex(int index);
    int dockedHeight() const { return m_dockedHeight; }
    void setDockedHeight(int height);
    double dockedScale() const { return m_dockedScale; }
    double compactScale() const { return m_compactScale; }
    bool autoCapitalize() const { return m_autoCapitalize; }
    bool doubleSpacePeriod() const { return m_doubleSpacePeriod; }
    QStringList recentEmoji() const { return m_recentEmoji; }
    QString fieldType() const { return m_fieldType; }
    bool autoShow() const { return m_autoShow; }

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setInputRegion(const QRectF &rect);
    Q_INVOKABLE void noteEmoji(const QString &emoji);
    // copy, cut, paste, undo, redo or selectAll on the focused field, with the
    // shortcut Omarchy's universal clipboard keys send: ctrl+shift+c and v in
    // a terminal, ctrl+c and v anywhere else.
    Q_INVOKABLE bool edit(const QString &action);
    static QStringList editActions();
    // The key (as text to type) and modifiers an edit action sends.
    static QPair<QString, int> editKeys(const QString &action, bool terminal);
    // The focused window is a terminal, where ctrl+c would interrupt.
    bool inTerminal() const;

    // A hardware keyboard is attached: fields no longer bring the on-screen
    // one up (unless autoShowWithKeyboard says so), and one that came up for
    // a field goes.
    void setHardwareKeyboard(bool attached);

    // There is a touch screen: a field brings the keyboard up only when the
    // user tapped into it, as on a tablet (unless autoShowAfterTap=false).
    // Tapping a field in another window, or the window whose field has
    // focus, counts; a field that takes focus by itself, in a window that
    // just opened or on a workspace swiped to, leaves the keyboard down until
    // it's tapped. `windowAt` names the focused window if a point of the
    // panel (as Taps gives it) is inside it, else returns empty.
    void setTouchScreen(std::function<QString(const QPointF &panel)> windowAt);
    // From Taps: a finger came down, and everything lifted again.
    void touchStarted();
    void touchEnded(bool tap, const QPointF &panel);
    // From Hyprland: a window opened, under this address.
    void windowOpened(const QString &address);

signals:
    void visibleChanged();
    void modeChanged();
    void floatingPositionChanged();
    void orbPositionChanged();
    void layoutIndexChanged();
    void dockedHeightChanged();
    void recentEmojiChanged();
    void fieldTypeChanged();
    // `floatkey clipboard`: show the keyboard on its clipboard page.
    void pageRequested(const QString &page);
    // `floatkey clip TEXT`: paste TEXT the way the clipboard page does.
    void clipRequested(const QString &text);

private:
    void setShown(bool shown);
    void focusChanged(const QString &fieldType);
    bool wantedFor(const QString &fieldType) const;
    bool tappedIntoFocus() const;
    void showForTap();
    void applyReservation();
    void applyInputRegion(bool force);

    QSettings m_settings;
    QPointer<QWindow> m_window;
    LayerShellQt::Window *m_layer = nullptr;
    VirtualKeyboard *m_keyboard = nullptr;
    QLocalServer *m_server = nullptr;
    QString m_socketPath;

    bool m_visible = false;
    QString m_mode = "docked";
    QPointF m_floatingPosition{0.78, 0.62};
    QPointF m_orbPosition{0.5, 0.9};
    QStringList m_layouts;
    QVariantMap m_layoutKeys;
    int m_layoutIndex = 0;
    int m_dockedHeight = 0;
    double m_dockedScale = 1.0;
    double m_compactScale = 1.0;
    bool m_autoCapitalize = false;
    bool m_doubleSpacePeriod = true;
    QStringList m_recentEmoji;
    QRect m_inputRegion;

    bool m_autoShow = true;
    bool m_autoShowInTerminals = true;
    bool m_autoShowWithKeyboard = false;
    bool m_hardwareKeyboard = false;
    QString m_fieldType;
    // Shown for the focused field, so it goes when the field loses focus.
    bool m_autoShown = false;
    QTimer m_autoHide;

    // The tap rule (setTouchScreen): where the last tap was and how long ago,
    // and the windows that opened since a finger last came down.
    std::function<QString(const QPointF &)> m_windowAt;
    bool m_autoShowAfterTap = true;
    bool m_touching = false;
    bool m_tapCounts = false;
    bool m_focusedWhileTouching = false;
    // Up for a tap into the window that had focus, so a window opening
    // right after takes it down again.
    bool m_shownForTap = false;
    QTimer m_tapCheck;
    QPointF m_tapAt;
    QElapsedTimer m_sinceTap;
    QSet<QString> m_newWindows;
};
