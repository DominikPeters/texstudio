#include "ViewerControls.h"
#include <QCoreApplication>
#include <QIntValidator>
#include <QActionGroup>
#include <cmath>

namespace ViewerControls {
static QString tr(const char *text) { return QCoreApplication::translate("PDFDocument", text); }
ActionSpec actionSpec(const QString &id)
{
    struct Definition { const char *id, *text, *icon; int shortcut; bool checkable; };
    static const Definition definitions[] = {
        {"magnify", QT_TRANSLATE_NOOP("PDFDocument", "&Magnify"), "magnifier-button", 0, true},
        {"scroll", QT_TRANSLATE_NOOP("PDFDocument", "&Scroll"), "hand", 0, true},
        {"selectText", QT_TRANSLATE_NOOP("PDFDocument", "&Select Text"), "cursor", 0, true},
        {"firstPage", QT_TRANSLATE_NOOP("PDFDocument", "&First Page"), "go-first", Qt::Key_Home, false},
        {"previous", QT_TRANSLATE_NOOP("PDFDocument", "&Previous Page"), "go-previous", 0, false},
        {"next", QT_TRANSLATE_NOOP("PDFDocument", "&Next Page"), "go-next", 0, false},
        {"last", QT_TRANSLATE_NOOP("PDFDocument", "&Last Page"), "go-last", Qt::Key_End, false},
        {"zoomIn", QT_TRANSLATE_NOOP("PDFDocument", "Zoom &In"), "zoom-in", Qt::CTRL | Qt::Key_Plus, false},
        {"zoomOut", QT_TRANSLATE_NOOP("PDFDocument", "Zoom &Out"), "zoom-out", Qt::CTRL | Qt::Key_Minus, false},
        {"actualSize", QT_TRANSLATE_NOOP("PDFDocument", "&Actual Size"), "zoom-original", Qt::CTRL | Qt::Key_1, false},
        {"fitToWidth", QT_TRANSLATE_NOOP("PDFDocument", "Fit to Wi&dth"), "zoom-fit-width", Qt::CTRL | Qt::Key_2, true},
        {"fitToTextWidth", QT_TRANSLATE_NOOP("PDFDocument", "Fit to &Text Width"), "zoom-fit-text-width", Qt::CTRL | Qt::Key_4, true},
        {"fitToWindow", QT_TRANSLATE_NOOP("PDFDocument", "Fit to &Window"), "zoom-fit-best", Qt::CTRL | Qt::Key_3, true},
        {"gotoSource", QT_TRANSLATE_NOOP("PDFDocument", "&Go to Source"), "", Qt::CTRL | Qt::Key_Apostrophe, false},
        {"followCursor", QT_TRANSLATE_NOOP("PDFDocument", "Scrolling follows cursor"), "syncViewer", 0, true}
    };
    for (const auto &d : definitions) if (id == QLatin1String(d.id)) {
        ActionSpec result{id, tr(d.text), QString::fromLatin1(d.icon), {}, d.checkable};
        if (d.shortcut) result.shortcuts << QKeySequence(d.shortcut);
        if (id == "firstPage") result.shortcuts << QKeySequence(Qt::CTRL | Qt::Key_Home);
        if (id == "last") result.shortcuts << QKeySequence(Qt::CTRL | Qt::Key_End);
        return result;
    }
    Q_ASSERT_X(false, "ViewerControls::actionSpec", "Unknown viewer action");
    return {};
}
QAction *createAction(const QString &id, QWidget *owner, const Icons &icons)
{
    const auto spec = actionSpec(id);
    auto action = new QAction(icons(spec.icon), spec.text, owner);
    action->setObjectName("viewer/" + id);
    action->setShortcuts(spec.shortcuts);
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    action->setCheckable(spec.checkable);
    owner->addAction(action);
    return action;
}
void addToolbarActions(QToolBar *toolbar, const QHash<QString, QAction *> &actions)
{
    // Missing capabilities are omitted rather than exposing inert controls.
    for (const auto &group : {QStringList{"magnify", "scroll", "selectText"},
             QStringList{"back", "forward"}, QStringList{"firstPage", "previous", "next", "last"},
             QStringList{"actualSize", "fitToWidth", "fitToTextWidth", "fitToWindow"}}) {
        bool added = false;
        for (const auto &id : group) if (auto action = actions.value(id)) { toolbar->addAction(action); added = true; }
        if (added) toolbar->addSeparator();
    }
}
PageControls insertPageControls(QToolBar *toolbar, QAction *before, bool modernStyle)
{
    PageControls controls;
    controls.number = new QLineEdit(toolbar);
    controls.number->setMaxLength(5);
    controls.number->setFixedWidth(controls.number->fontMetrics().horizontalAdvance("#####"));
    controls.validator = new QIntValidator(1, 99999, controls.number);
    controls.number->setValidator(controls.validator);
    controls.number->setText("1");
    controls.number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    controls.number->setStyleSheet(modernStyle ? "QLineEdit{ color: white; padding-top: -1px; margin-right: 2px; }"
                                               : "QLineEdit{ padding-top: -1px; margin-right: 2px; }");
    controls.separator = new QLabel(QCoreApplication::translate("PDFDocument", "of", "separator for page number: 1 of 3"), toolbar);
    controls.count = new QLabel("1", toolbar);
    for (auto label : {controls.separator, controls.count}) label->setAlignment(Qt::AlignCenter);
    for (QWidget *widget : QList<QWidget *>{controls.number, controls.separator, controls.count}) toolbar->insertWidget(before, widget);
    return controls;
}
ZoomControls createZoomControls(QWidget *owner, const Icons &icons,
    const std::function<void(qreal)> &zoom, const std::function<void()> &custom)
{
    ZoomControls controls;
    controls.scale = new QToolButton(owner);
    controls.scale->setToolTip(tr("Scale"));
    controls.scale->setPopupMode(QToolButton::InstantPopup);
    controls.scale->setAutoRaise(true);
    controls.scale->setMinimumWidth(owner->fontMetrics().horizontalAdvance("OOOOOO"));
    controls.scale->setText("100%");
    for (int level : {25, 50, 75, 100, 150, 200, 300, 400}) {
        auto action = new QAction(QString::number(level) + '%', controls.scale);
        action->setData(level);
        QObject::connect(action, &QAction::triggered, owner, [zoom, level] { zoom(level / 100.0); });
        controls.scale->addAction(action);
    }
    auto action = new QAction(tr("Custom..."), controls.scale);
    QObject::connect(action, &QAction::triggered, owner, custom);
    controls.scale->addAction(action);
    controls.out = new QToolButton(owner); controls.out->setIcon(icons("zoom-out")); controls.out->setToolTip(tr("Zoom Out"));
    controls.in = new QToolButton(owner); controls.in->setIcon(icons("zoom-in")); controls.in->setToolTip(tr("Zoom In"));
    controls.slider = new QSlider(Qt::Horizontal, owner);
    controls.slider->setRange(-100, 100); controls.slider->setSingleStep(25); controls.slider->setFixedWidth(100);
    controls.slider->setToolTip(tr("Zoom"));
    return controls;
}
qreal sliderToScale(int position)
{
    if (std::abs(position) < 10) return 1;
    return position < 0 ? (1 - 0.125) / 90 * (position + 10) + 1 : (4.0 - 1) / 90 * (position - 10) + 1;
}
int scaleToSlider(qreal scale)
{
    if (std::abs(scale - 1) < .01) return 0;
    return scale < 1 ? qRound((scale - 1) / (1 - .125) * 90 - 10) : qRound((scale - 1) / (4.0 - 1) * 90 + 10);
}
qreal steppedScale(qreal scale, int direction, qreal step)
{
    step = qBound(1.001, step, 10.0);
    scale = direction > 0 ? scale * step : scale / step;
    if (std::abs(scale - qRound(scale)) < .01) scale = qRound(scale);
    return qBound(.125, scale, 4.0);
}
bool isSyncClick(Qt::KeyboardModifiers modifiers)
{
    return (modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier);
}
}
