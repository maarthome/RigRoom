#pragma once

#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QSize>
#include <QWidget>
#include <QtGlobal>

// Keeps windows inside the screen they open on, for small displays such as a
// Raspberry Pi touchscreen, where the default sizes would hang off the edge.
namespace ScreenFit {

// The usable part of the screen a widget is on (the whole screen when no
// window manager reserves panels).
inline QRect availableArea(const QWidget* widget = nullptr) {
    QScreen* screen = widget ? widget->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect();
}

// A size no larger than the screen allows.
inline QSize bounded(const QSize& wanted, const QWidget* widget = nullptr) {
    const QRect area = availableArea(widget);
    return area.isValid() ? wanted.boundedTo(area.size()) : wanted;
}

inline int boundedWidth(int wanted, const QWidget* widget = nullptr) {
    const QRect area = availableArea(widget);
    return area.isValid() ? qMin(wanted, area.width()) : wanted;
}

inline int boundedHeight(int wanted, const QWidget* widget = nullptr) {
    const QRect area = availableArea(widget);
    return area.isValid() ? qMin(wanted, area.height()) : wanted;
}

// The usable screen area in device pixels, the unit plugin GUIs work in
// (Qt's interface scale does not apply to them).
inline QSize physicalArea(const QWidget* widget = nullptr) {
    QScreen* screen = widget ? widget->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return QSize();
    const QRect area = screen->availableGeometry();
    const qreal ratio = screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0;
    return QSize(qRound(area.width() * ratio), qRound(area.height() * ratio));
}

// Height of the bar with a Close button above every plugin GUI. Without a
// window manager (a touchscreen running only RigRoom) windows have no title
// bar, so this is the only way to close them.
inline constexpr int kPluginBarHeight = 56;

// How much a plugin view of the given device-pixel size must shrink to fit
// the screen below the plugin bar: 1.0 when it already fits.
inline double shrinkToFit(int width, int height, const QWidget* widget = nullptr) {
    QSize room = physicalArea(widget);
    if (!room.isValid() || width <= 0 || height <= 0) return 1.0;
    QScreen* screen = widget ? widget->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    const qreal ratio = screen && screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0;
    room.setHeight(qMax(1, room.height() - qRound(kPluginBarHeight * ratio)));
    return qMin(1.0, qMin(room.width() / double(width), room.height() / double(height)));
}

inline bool fitsScreen(int width, int height, const QWidget* widget = nullptr) {
    return shrinkToFit(width, height, widget) >= 1.0;
}

// Put the plugin bar (name and Close button) at the top of a plugin window.
inline void addPluginBar(QWidget* window, QVBoxLayout* layout, const QString& title) {
    auto* bar = new QWidget(window);
    bar->setObjectName("pluginBar");
    bar->setFixedHeight(kPluginBarHeight);
    // Large enough to hit with a finger on a small touchscreen.
    bar->setStyleSheet("QWidget#pluginBar { background: #2A2A30; border-bottom: 2px solid #00B0FF; }"
                       "QLabel { color: #F0F0F0; font-weight: bold; font-size: 16px; background: transparent; }"
                       "QPushButton { background: #C62828; color: #FFFFFF; border: none;"
                       " border-radius: 6px; padding: 4px 22px; font-weight: bold; font-size: 18px; }"
                       "QPushButton:pressed { background: #8E0000; }");
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 3, 4, 3);
    auto* label = new QLabel(title, bar);
    row->addWidget(label, 1);
    auto* close = new QPushButton(QString::fromUtf8("CLOSE \u2715"), bar);
    close->setFocusPolicy(Qt::NoFocus);
    close->setMinimumHeight(kPluginBarHeight - 10);
    close->setMinimumWidth(140);
    QObject::connect(close, &QPushButton::clicked, window, &QWidget::close);
    row->addWidget(close);
    layout->insertWidget(0, bar);
}

// Shrink a top-level window that is larger than the screen and move it so it
// lies fully on the screen.
inline void fitWindow(QWidget* window) {
    const QRect area = availableArea(window);
    if (!area.isValid()) return;

    // Room taken by the window manager's frame; zero without a window manager
    // or before the window has been shown.
    const QSize frame = window->frameGeometry().size() - window->geometry().size();
    const QSize room(qMax(1, area.width() - frame.width()), qMax(1, area.height() - frame.height()));

    QSize size = window->size();
    if (size.width() > room.width() || size.height() > room.height()) {
        window->setMinimumSize(window->minimumSize().boundedTo(room));
        size = size.boundedTo(room);
        window->resize(size);
    }

    QPoint pos = window->pos();
    const int outerWidth = size.width() + frame.width();
    const int outerHeight = size.height() + frame.height();
    pos.setX(qBound(area.left(), pos.x(), qMax(area.left(), area.right() + 1 - outerWidth)));
    pos.setY(qBound(area.top(), pos.y(), qMax(area.top(), area.bottom() + 1 - outerHeight)));
    if (pos != window->pos()) window->move(pos);
}

// Put a page in a frameless scroll area, so it can be scrolled when the window
// is shorter or narrower than the page.
inline QScrollArea* scrollable(QWidget* page, QWidget* parent = nullptr) {
    auto* area = new QScrollArea(parent);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setWidget(page);
    area->setStyleSheet("QScrollArea { background: transparent; border: none; }"
                        "QScrollArea > QWidget > QWidget { background: transparent; }");
    return area;
}

// Application-wide: every dialog is fitted to the screen as it opens.
class DialogFitter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show && watched->isWidgetType()) {
            auto* widget = static_cast<QWidget*>(watched);
            if (widget->isWindow() && qobject_cast<QDialog*>(widget)) fitWindow(widget);
        }
        return false;
    }
};

} // namespace ScreenFit
