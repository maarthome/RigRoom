#pragma once

#include <QApplication>
#include <QFont>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QtGlobal>
#include <QtMath>

// Text size chosen in Settings. Stylesheets are built as widgets are created,
// so the size is read once at startup and a change takes effect on the next
// launch.
namespace UiScale {

inline constexpr const char* kSettingKey = "ui_text_scale";
inline constexpr int kDefaultPercent = 100;

inline int clampPercent(int percent) { return qBound(70, percent, 200); }

inline int percentSetting() {
    return clampPercent(QSettings("RigRoom", "RigRoom").value(kSettingKey, kDefaultPercent).toInt());
}

inline void setPercentSetting(int percent) {
    QSettings("RigRoom", "RigRoom").setValue(kSettingKey, clampPercent(percent));
}

// Interface scale (the whole window, not only text). 0 means automatic: fit
// the usual 1200x800 layout on the screen. Applied at startup.
inline constexpr const char* kScreenScaleKey = "ui_screen_scale";
inline constexpr int kDesignWidth = 1200;
inline constexpr int kDesignHeight = 800;

inline int screenScaleSetting() {
    const int percent = QSettings("RigRoom", "RigRoom").value(kScreenScaleKey, 0).toInt();
    return percent <= 0 ? 0 : qBound(40, percent, 200);
}

inline void setScreenScaleSetting(int percent) {
    QSettings("RigRoom", "RigRoom").setValue(kScreenScaleKey, percent <= 0 ? 0 : qBound(40, percent, 200));
}

// The scale for a screen of the given size: the chosen one, or (automatic)
// the largest one at which the usual layout fits, never above 100%.
inline double screenScaleFor(int screenWidth, int screenHeight) {
    const int chosen = screenScaleSetting();
    if (chosen > 0) return chosen / 100.0;
    if (screenWidth <= 0 || screenHeight <= 0) return 1.0;
    const double fit = qMin(screenWidth / double(kDesignWidth), screenHeight / double(kDesignHeight));
    if (fit >= 1.0) return 1.0;
    return qMax(0.4, qFloor(fit * 100.0) / 100.0);
}

inline double& factorRef() {
    static double factor = 1.0;
    return factor;
}

inline double factor() { return factorRef(); }

// Read the saved size and scale the application's base font to match.
// Call once, right after QApplication exists and before any window is built.
inline void initialize(QApplication& app) {
    factorRef() = percentSetting() / 100.0;
    QFont font = app.font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * factor());
        app.setFont(font);
    }
}

// Scale every "font-size: Npx" in a stylesheet by the chosen factor. Other
// rules are left as they are.
inline QString css(const QString& sheet) {
    const double f = factor();
    if (qFuzzyCompare(f, 1.0)) return sheet;
    static const QRegularExpression pattern(QStringLiteral("(font-size\\s*:\\s*)(\\d+(?:\\.\\d+)?)px"));
    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = pattern.globalMatch(sheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += sheet.mid(last, match.capturedStart() - last);
        const double px = match.captured(2).toDouble();
        result += match.captured(1) + QString::number(qMax(1, qRound(px * f))) + QStringLiteral("px");
        last = match.capturedEnd();
    }
    result += sheet.mid(last);
    return result;
}

inline QString css(const char* sheet) { return css(QString::fromUtf8(sheet)); }

} // namespace UiScale
