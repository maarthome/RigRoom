#pragma once

#include <QAbstractButton>
#include <QChildEvent>
#include <QEvent>
#include <QFont>
#include <QLayout>
#include <QObject>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>

// Scales one part of the interface (the toolbar, the preset panel, the board's
// corner controls) by its own percentage from Settings > GUI: stylesheet sizes,
// fixed and minimum/maximum sizes, layout margins, icons and fonts. Widgets
// added to the part later, and stylesheets set again later, are scaled too.
// Applied at startup.
namespace SectionScale {

inline int percent(const char* key) {
    return qBound(50, QSettings("RigRoom", "RigRoom").value(key, 100).toInt(), 300);
}

inline void setPercent(const char* key, int value) {
    QSettings("RigRoom", "RigRoom").setValue(key, qBound(50, value, 300));
}

inline int scaled(int value, double factor) {
    return value <= 0 ? value : qMax(1, qRound(value * factor));
}

// Every "<number>px" in a stylesheet, times factor.
inline QString scaleSheet(const QString& sheet, double factor) {
    static const QRegularExpression pattern(QStringLiteral("(\\d+(?:\\.\\d+)?)px"));
    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = pattern.globalMatch(sheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += sheet.mid(last, match.capturedStart() - last);
        const double value = match.captured(1).toDouble();
        const int out = value <= 0.0 ? 0 : qMax(1, qRound(value * factor));
        result += QString::number(out) + QStringLiteral("px");
        last = match.capturedEnd();
    }
    result += sheet.mid(last);
    return result;
}

class Scaler : public QObject {
public:
    Scaler(double factor, QObject* parent) : QObject(parent), m_factor(factor) {}

    void scaleTree(QWidget* root) {
        if (!root) return;
        scaleWidget(root);
        const auto children = root->findChildren<QWidget*>();
        for (QWidget* child : children) scaleWidget(child);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!watched->isWidgetType()) return false;
        if (event->type() == QEvent::ChildPolished) {
            // A widget added later (e.g. scene buttons rebuilt for a preset).
            auto* childEvent = static_cast<QChildEvent*>(event);
            if (childEvent->child() && childEvent->child()->isWidgetType())
                scaleTree(static_cast<QWidget*>(childEvent->child()));
        } else if (event->type() == QEvent::StyleChange) {
            // A stylesheet set again later (e.g. the Save button's colour).
            rescaleSheet(static_cast<QWidget*>(watched));
        }
        return false;
    }

private:
    void rescaleSheet(QWidget* widget) {
        const QString sheet = widget->styleSheet();
        if (sheet.isEmpty() || sheet == widget->property("uiScaledSheet").toString()) return;
        const QString result = scaleSheet(sheet, m_factor);
        widget->setProperty("uiScaledSheet", result);
        if (result != sheet) widget->setStyleSheet(result);
    }

    void scaleWidget(QWidget* widget) {
        if (widget->property("uiScaled").toBool()) return;
        widget->setProperty("uiScaled", true);
        widget->setProperty("uiScale", m_factor);  // for widgets that paint their own text

        const QSize minimum = widget->minimumSize();
        const QSize maximum = widget->maximumSize();
        auto scaleMax = [&]() {
            if (maximum.width() < QWIDGETSIZE_MAX) widget->setMaximumWidth(scaled(maximum.width(), m_factor));
            if (maximum.height() < QWIDGETSIZE_MAX) widget->setMaximumHeight(scaled(maximum.height(), m_factor));
        };
        auto scaleMin = [&]() {
            if (minimum.width() > 0) widget->setMinimumWidth(scaled(minimum.width(), m_factor));
            if (minimum.height() > 0) widget->setMinimumHeight(scaled(minimum.height(), m_factor));
        };
        // Keep minimum <= maximum at every step.
        if (m_factor >= 1.0) { scaleMax(); scaleMin(); } else { scaleMin(); scaleMax(); }

        if (QLayout* layout = widget->layout()) {
            const QMargins m = layout->contentsMargins();
            layout->setContentsMargins(scaled(m.left(), m_factor), scaled(m.top(), m_factor),
                                       scaled(m.right(), m_factor), scaled(m.bottom(), m_factor));
            if (layout->spacing() > 0) layout->setSpacing(scaled(layout->spacing(), m_factor));
        }
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            const QSize icon = button->iconSize();
            if (icon.isValid()) button->setIconSize(QSize(scaled(icon.width(), m_factor), scaled(icon.height(), m_factor)));
        }
        rescaleSheet(widget);
        widget->installEventFilter(this);
        widget->updateGeometry();  // size hints may depend on the scale
    }

    double m_factor = 1.0;
};

// Scale `root` and everything in it by the percentage stored under `key`.
inline void apply(QWidget* root, const char* key) {
    if (!root) return;
    const double factor = percent(key) / 100.0;
    if (qFuzzyCompare(factor, 1.0)) return;
    // Text not sized by a stylesheet follows the root's font.
    QFont font = root->font();
    if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * factor);
    else if (font.pixelSize() > 0) font.setPixelSize(scaled(font.pixelSize(), factor));
    root->setFont(font);
    auto* scaler = new Scaler(factor, root);
    scaler->scaleTree(root);
}

// The factor a widget was scaled by (1.0 if none), for custom painting.
inline double factorOf(const QWidget* widget) {
    const QVariant value = widget ? widget->property("uiScale") : QVariant();
    return value.isValid() ? value.toDouble() : 1.0;
}

} // namespace SectionScale
