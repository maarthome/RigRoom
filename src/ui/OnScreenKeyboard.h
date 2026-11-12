#pragma once

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <QTextCursor>
#include <cstdio>

// A touch keyboard for screens without a physical one. It appears at the
// bottom of the screen when a text or number field gets focus; if it would
// cover that field, the field's window moves up until the field is visible
// above the keyboard, and moves back when the keyboard goes away.
// Settings > GUI > On-screen Keyboard turns it off.
class OnScreenKeyboard : public QWidget {
public:
    static inline const char* kSettingKey = "gui_onscreen_keyboard";

    static bool enabled() { return QSettings("RigRoom", "RigRoom").value(kSettingKey, true).toBool(); }

    static OnScreenKeyboard* instance() {
        static QPointer<OnScreenKeyboard> keyboard;
        if (!keyboard) keyboard = new OnScreenKeyboard();
        return keyboard;
    }

    // Watch the whole application for text fields getting focus.
    static void install(QApplication& app);

    static bool isTextField(QWidget* widget) {
        if (!widget || !widget->isEnabled()) return false;
        if (auto* edit = qobject_cast<QLineEdit*>(widget)) return !edit->isReadOnly();
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) return !spin->isReadOnly();
        if (auto* combo = qobject_cast<QComboBox*>(widget)) return combo->isEditable();
        if (auto* text = qobject_cast<QTextEdit*>(widget)) return !text->isReadOnly();
        if (auto* plain = qobject_cast<QPlainTextEdit*>(widget)) return !plain->isReadOnly();
        return false;
    }

    // Spin boxes and editable combo boxes open the keyboard only when their
    // text is tapped, not their arrows.
    static bool opensOnFocus(QWidget* widget) {
        return isTextField(widget) && !qobject_cast<QAbstractSpinBox*>(widget) && !qobject_cast<QComboBox*>(widget)
            && !(widget->parentWidget() && (qobject_cast<QAbstractSpinBox*>(widget->parentWidget())
                                            || qobject_cast<QComboBox*>(widget->parentWidget())));
    }

    // The field a tap on `widget` edits: the widget itself, or the spin box or
    // combo box whose text part it is.
    static QWidget* fieldFor(QWidget* widget) {
        if (!widget) return nullptr;
        QWidget* parent = widget->parentWidget();
        if (parent && (qobject_cast<QTextEdit*>(parent) || qobject_cast<QPlainTextEdit*>(parent)))
            return isTextField(parent) ? parent : nullptr;  // their viewport gets the taps
        if (qobject_cast<QLineEdit*>(widget) && parent
            && (qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent))) {
            return isTextField(parent) ? parent : nullptr;
        }
        return isTextField(widget) ? widget : nullptr;
    }

    QWidget* field() const { return m_field; }

    void showFor(QWidget* field) {
        if (!field) return;
        if (field != m_field) restoreWindow();
        m_field = field;
        // Belong to the field's window: a modal dialog (e.g. the one for typing
        // a parameter value) blocks taps on every other window, the keyboard
        // included, unless the keyboard is its child.
        if (QWidget* host = field->window(); host && host != this && parentWidget() != host) {
            const Qt::WindowFlags flags = windowFlags();
            setParent(host, flags);
        }
        QScreen* screen = field->screen() ? field->screen() : QGuiApplication::primaryScreen();
        if (!screen) return;
        const QRect area = screen->availableGeometry();
        const int height = qMax(180, area.height() * 40 / 100);
        // Fixed: as the child of a dialog the keys would otherwise take on its
        // button style and grow the keyboard past the bottom of the screen.
        setFixedSize(area.width(), height);
        move(area.left(), area.bottom() + 1 - height);
        show();
        raise();
        // Give the field's window the keyboard focus explicitly. Without a
        // window manager X otherwise hands focus to whatever window the finger
        // is on, and tapping a key would take it away from the field.
        if (QWidget* window = field->window()) window->activateWindow();
        field->setFocus(Qt::OtherFocusReason);
        // A number is typed anew: select it so the first key replaces it
        // (also a special text such as "Auto").
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(field)) spin->selectAll();
        keepFieldVisible();
    }

    void hideKeyboard() {
        hide();
        restoreWindow();
        m_field = nullptr;
    }

private:
    OnScreenKeyboard() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                                              | Qt::WindowDoesNotAcceptFocus) {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_X11DoNotAcceptFocus);
        setFocusPolicy(Qt::NoFocus);
        setObjectName("onScreenKeyboard");
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(
            "QWidget#onScreenKeyboard { background: #1B1C20; border-top: 2px solid #00B0FF; }"
            "QPushButton { background: #2E3038; color: #F0F0F3; border: 1px solid #3E414B; border-radius: 6px;"
            " font-size: 20px; font-weight: bold; padding: 0px; margin: 0px; min-width: 0px; min-height: 0px; }"
            "QPushButton:pressed { background: #00B0FF; color: #07121A; }"
            "QPushButton[special=\"true\"] { background: #3A3D47; font-size: 16px; }"
            "QPushButton[active=\"true\"] { background: #0B4F6C; }");
        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setSpacing(5);

        // Each row is 20 units wide; a normal key takes 2 units.
        const QStringList rows[] = {
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
            {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
            {"a", "s", "d", "f", "g", "h", "j", "k", "l"},
            {"z", "x", "c", "v", "b", "n", "m", "-", "."},
        };
        for (int r = 0; r < 4; ++r) {
            int column = (r == 2) ? 1 : 0;
            if (r == 3) {
                addSpecial(grid, r, column, 2, QString::fromUtf8("⇧"), [this]() { setShift(!m_shift); }, &m_shiftKey);
                column += 2;
            }
            for (const QString& key : rows[r]) {
                QPushButton* button = addKey(grid, r, column, 2, key);
                if (key.at(0).isLetter()) m_letterKeys.push_back(button);
                column += 2;
            }
            if (r == 0) addSpecial(grid, r, column, 2, QString::fromUtf8("⌫"), [this]() { sendKey(Qt::Key_Backspace, QString()); });
            if (r == 2) addSpecial(grid, r, column, 3, QString::fromUtf8("⏎"), [this]() { sendKey(Qt::Key_Return, QStringLiteral("\r")); });
        }
        int column = 0;
        addSpecial(grid, 4, column, 3, QString::fromUtf8("▼ Hide"), [this]() { hideKeyboard(); });
        column += 3;
        addKey(grid, 4, column, 2, "_");
        column += 2;
        addKey(grid, 4, column, 2, "/");
        column += 2;
        addSpecial(grid, 4, column, 8, "space", [this]() { sendKey(Qt::Key_Space, QStringLiteral(" ")); });
        column += 8;
        addSpecial(grid, 4, column, 2, QString::fromUtf8("◀"), [this]() { sendKey(Qt::Key_Left, QString()); });
        column += 2;
        addSpecial(grid, 4, column, 2, QString::fromUtf8("▶"), [this]() { sendKey(Qt::Key_Right, QString()); });
        for (int c = 0; c < 22; ++c) grid->setColumnStretch(c, 1);
        for (int r = 0; r < 5; ++r) grid->setRowStretch(r, 1);
    }

    QPushButton* makeButton(const QString& label) {
        auto* button = new QPushButton(label, this);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoRepeat(false);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        button->setMinimumSize(1, 1);
        return button;
    }

    QPushButton* addKey(QGridLayout* grid, int row, int column, int span, const QString& text) {
        QPushButton* button = makeButton(text);
        grid->addWidget(button, row, column, 1, span);
        connect(button, &QPushButton::clicked, this, [this, button]() {
            const QString text = button->text();
            const QChar c = text.at(0);
            int key = Qt::Key_unknown;
            if (c.isLetter()) key = Qt::Key_A + (c.toUpper().unicode() - 'A');
            else if (c.isDigit()) key = Qt::Key_0 + (c.unicode() - '0');
            else if (c == '-') key = Qt::Key_Minus;
            else if (c == '.') key = Qt::Key_Period;
            else if (c == '_') key = Qt::Key_Underscore;
            else if (c == '/') key = Qt::Key_Slash;
            sendKey(key, text);
            if (m_shift && c.isLetter()) setShift(false);  // one capital, like a phone
        });
        return button;
    }

    template <typename Action>
    QPushButton* addSpecial(QGridLayout* grid, int row, int column, int span, const QString& label, Action action,
                            QPushButton** keep = nullptr) {
        QPushButton* button = makeButton(label);
        button->setProperty("special", true);
        grid->addWidget(button, row, column, 1, span);
        connect(button, &QPushButton::clicked, this, action);
        if (keep) *keep = button;
        return button;
    }

    void setShift(bool on) {
        m_shift = on;
        for (QPushButton* key : m_letterKeys) key->setText(on ? key->text().toUpper() : key->text().toLower());
        if (m_shiftKey) {
            m_shiftKey->setProperty("active", on);
            m_shiftKey->style()->unpolish(m_shiftKey);
            m_shiftKey->style()->polish(m_shiftKey);
        }
    }

    void sendKey(int key, const QString& text) {
        // Always the field the keyboard was opened for, whatever has focus.
        QWidget* target = m_field;
        if (!target) target = QApplication::focusWidget();
        if (!target || target == this || isAncestorOf(target)) return;
        if (QWidget* window = target->window()) {
            if (!window->isActiveWindow()) window->activateWindow();
        }
        if (!target->hasFocus()) target->setFocus(Qt::OtherFocusReason);
        std::fprintf(stderr, "OnScreenKeyboard: key '%s' -> %s\n", qPrintable(text), target->metaObject()->className());

        // Edit the text directly; synthetic key presses are not taken by every
        // field. A spin box or editable combo box is edited through its text
        // part, and reads the number as it changes.
        QLineEdit* edit = qobject_cast<QLineEdit*>(target);
        if (!edit && (qobject_cast<QAbstractSpinBox*>(target) || qobject_cast<QComboBox*>(target)))
            edit = target->findChild<QLineEdit*>();
        if (edit && key != Qt::Key_Return) {
            if (key == Qt::Key_Backspace) edit->backspace();
            else if (key == Qt::Key_Left) edit->cursorBackward(false);
            else if (key == Qt::Key_Right) edit->cursorForward(false);
            else edit->insert(text);
            return;
        }
        if (auto* textEdit = qobject_cast<QTextEdit*>(target); textEdit && key != Qt::Key_Return) {
            if (key == Qt::Key_Backspace) textEdit->textCursor().deletePreviousChar();
            else if (!text.isEmpty()) textEdit->insertPlainText(text);
            return;
        }
        if (auto* plain = qobject_cast<QPlainTextEdit*>(target); plain && key != Qt::Key_Return) {
            if (key == Qt::Key_Backspace) plain->textCursor().deletePreviousChar();
            else if (!text.isEmpty()) plain->insertPlainText(text);
            return;
        }
        // Enter (finishes a value, presses a dialog's default button), and
        // anything else, as a key press.
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(target, &release);
        if (key == Qt::Key_Return) hideKeyboard();
    }

    // Move the field's window up so the field sits above the keyboard.
    void keepFieldVisible() {
        if (!m_field || !isVisible()) return;
        QWidget* window = m_field->window();
        if (!window || window == this) return;
        const QRect field(m_field->mapToGlobal(QPoint(0, 0)), m_field->size());
        const int keyboardTop = geometry().top();
        const int overlap = field.bottom() + 12 - keyboardTop;
        if (overlap <= 0) return;
        if (!m_movedWindow) {
            m_movedWindow = window;
            m_movedFrom = window->pos();
        }
        window->move(window->x(), window->y() - overlap);
    }

    void restoreWindow() {
        if (m_movedWindow) m_movedWindow->move(m_movedFrom);
        m_movedWindow = nullptr;
    }

    bool m_shift = false;
    QPushButton* m_shiftKey = nullptr;
    QList<QPushButton*> m_letterKeys;
    QPointer<QWidget> m_field;
    QPointer<QWidget> m_movedWindow;
    QPoint m_movedFrom;
};

// Shows the keyboard when a text field gets focus and hides it when focus
// moves to something that is not one.
class OnScreenKeyboardWatcher : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        const QEvent::Type type = event->type();
        if (type != QEvent::FocusOut && type != QEvent::MouseButtonRelease) return false;
        if (!watched->isWidgetType()) return false;
        auto* widget = static_cast<QWidget*>(watched);
        OnScreenKeyboard* keyboard = OnScreenKeyboard::instance();
        if (widget == keyboard || keyboard->isAncestorOf(widget)) return false;

        // Opens only when a field is tapped, not when a window puts the cursor
        // in a field by itself (e.g. the plugin list's search box).
        if (event->type() == QEvent::MouseButtonRelease) {
            // Tapping the text of a field (also of a spin box or editable combo,
            // and again after the keyboard was hidden) opens the keyboard.
            QWidget* field = OnScreenKeyboard::fieldFor(widget);
            const bool onText = qobject_cast<QLineEdit*>(widget) || qobject_cast<QTextEdit*>(widget)
                             || qobject_cast<QPlainTextEdit*>(widget)
                             || (widget->parentWidget() && (qobject_cast<QTextEdit*>(widget->parentWidget())
                                                            || qobject_cast<QPlainTextEdit*>(widget->parentWidget())));
            if (field && onText && OnScreenKeyboard::enabled()
                && (!keyboard->isVisible() || keyboard->field() != field)) {
                QPointer<QWidget> target = field;
                QTimer::singleShot(0, [target]() {
                    if (target) OnScreenKeyboard::instance()->showFor(target);
                });
            }
        } else if (event->type() == QEvent::FocusOut && OnScreenKeyboard::isTextField(widget)) {
            // Losing focus because another window became active (e.g. a tap on
            // the keyboard itself) is not leaving the field.
            const auto reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason == Qt::ActiveWindowFocusReason || reason == Qt::PopupFocusReason) return false;
            QTimer::singleShot(0, []() {
                QWidget* now = QApplication::focusWidget();
                OnScreenKeyboard* keyboard = OnScreenKeyboard::instance();
                if (now && (now == keyboard || keyboard->isAncestorOf(now))) return;
                if (!now || !OnScreenKeyboard::fieldFor(now)) keyboard->hideKeyboard();
            });
        }
        return false;
    }
};

inline void OnScreenKeyboard::install(QApplication& app) {
    app.installEventFilter(new OnScreenKeyboardWatcher(&app));
}
