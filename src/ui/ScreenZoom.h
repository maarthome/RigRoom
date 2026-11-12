#pragma once

#include <QProcess>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// Zooms the whole X screen (xrandr --scale) so that one plugin GUI fills the
// panel, and restores the previous scale when the last plugin window closes.
// The X server does the scaling, so it works for every plugin, including GUIs
// that cannot draw themselves at another size. Needs the xrandr tool
// (x11-xserver-utils); without it nothing changes.
namespace ScreenZoom {

struct Panel {
    QString output;  // first connected output, e.g. DSI-1
    QSize mode;      // its real resolution in pixels
    QSize screen;    // the X screen size, i.e. mode x current scale
    bool valid = false;
};

inline Panel query() {
    Panel panel;
    QProcess xrandr;
    xrandr.start("xrandr", {"--query"});
    if (!xrandr.waitForFinished(3000)) return panel;
    const QStringList lines = QString::fromLocal8Bit(xrandr.readAllStandardOutput()).split('\n');
    static const QRegularExpression screenPattern("current (\\d+) x (\\d+)");
    static const QRegularExpression modePattern("^\\s+(\\d+)x(\\d+)\\S*\\s.*\\*");
    bool inOutput = false;
    for (const QString& line : lines) {
        if (line.startsWith("Screen ")) {
            const QRegularExpressionMatch match = screenPattern.match(line);
            if (match.hasMatch()) panel.screen = QSize(match.captured(1).toInt(), match.captured(2).toInt());
            continue;
        }
        if (!line.startsWith(' ') && !line.startsWith('\t')) {
            inOutput = false;
            if (panel.output.isEmpty() && line.contains(" connected")) {
                panel.output = line.section(' ', 0, 0);
                inOutput = true;
            }
            continue;
        }
        if (inOutput && !panel.mode.isValid()) {
            const QRegularExpressionMatch match = modePattern.match(line);
            if (match.hasMatch()) panel.mode = QSize(match.captured(1).toInt(), match.captured(2).toInt());
        }
    }
    panel.valid = !panel.output.isEmpty() && panel.mode.isValid() && !panel.mode.isEmpty() && panel.screen.isValid();
    return panel;
}

inline bool setScale(const QString& output, double scale) {
    const QString value = QString::number(scale, 'f', 4);
    return QProcess::execute("xrandr", {"--output", output, "--scale", value + "x" + value}) == 0;
}

struct State {
    int users = 0;
    double baseline = 0.0;
    QString output;
};

inline State& state() {
    static State current;
    return current;
}

// Zoom so that content (a size in X pixels) fills the panel. Returns the new
// X screen size, or an invalid size when the zoom could not be changed.
// Every call must be matched by one leave(), whatever it returned.
inline QSize enter(const QSize& content) {
    State& current = state();
    if (current.users == 0) {
        current.baseline = 0.0;
        current.output.clear();
    }
    ++current.users;
    if (content.isEmpty()) return QSize();
    const Panel panel = query();
    if (!panel.valid) return QSize();
    if (current.baseline <= 0.0) {
        current.baseline = panel.screen.width() / double(panel.mode.width());
        current.output = panel.output;
    }
    const double fill = qMax(content.width() / double(panel.mode.width()),
                             content.height() / double(panel.mode.height()));
    const double scale = qBound(0.5, fill, 4.0);
    if (!setScale(panel.output, scale)) return QSize();
    return QSize(qRound(scale * panel.mode.width()), qRound(scale * panel.mode.height()));
}

// One plugin window closed; the last one brings back the previous scale.
inline void leave() {
    State& current = state();
    if (current.users <= 0) return;
    if (--current.users == 0 && current.baseline > 0.0) setScale(current.output, current.baseline);
}

} // namespace ScreenZoom
