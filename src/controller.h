#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QSettings>
#include <QStringList>
#include <QTimer>

class QLocalServer;
class QWindow;
class VirtualKeyboard;
namespace LayerShellQt {
class Window;
}

// The keyboard's state: shown or hidden, where it sits (docked, floating, or in
// a corner), which layout it shows, and the user's settings. It also tells the
// compositor which part of the transparent overlay takes touches, and reserves
// space at the bottom of the screen while the keyboard is docked so windows
// move up instead of hiding behind it. With fcitx5's floatkey addon it comes
// up by itself when a text field takes focus, and goes when it loses it.
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QPointF floatingPosition READ floatingPosition WRITE setFloatingPosition NOTIFY floatingPositionChanged)
    Q_PROPERTY(QStringList layouts READ layouts CONSTANT)
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
    QStringList layouts() const { return m_layouts; }
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

signals:
    void visibleChanged();
    void modeChanged();
    void floatingPositionChanged();
    void layoutIndexChanged();
    void dockedHeightChanged();
    void recentEmojiChanged();
    void fieldTypeChanged();

private:
    void setShown(bool shown);
    void focusChanged(const QString &fieldType);
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
    QStringList m_layouts;
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
    QString m_fieldType;
    // Shown for the focused field, so it goes when the field loses focus.
    bool m_autoShown = false;
    QTimer m_autoHide;
};
