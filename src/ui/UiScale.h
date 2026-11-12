#pragma once

#include <QApplication>
#include <QFont>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QtGlobal>

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
