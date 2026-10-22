#pragma once
#include <QAction>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QIntValidator>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <functional>

// Rendering-independent controls shared by the saved-PDF and live viewers.
namespace ViewerControls {
using Icons = std::function<QIcon(const QString &)>;
struct ActionSpec {
    QString id, text, icon;
    QList<QKeySequence> shortcuts;
    bool checkable = false;
};
ActionSpec actionSpec(const QString &id);
QAction *createAction(const QString &id, QWidget *owner, const Icons &icons);
void addToolbarActions(QToolBar *toolbar, const QHash<QString, QAction *> &actions);
struct PageControls {
    QLineEdit *number;
    QIntValidator *validator;
    QLabel *separator, *count;
};
PageControls insertPageControls(QToolBar *toolbar, QAction *before, bool modernStyle);
struct ZoomControls {
    QToolButton *scale, *out, *in;
    QSlider *slider;
};
ZoomControls createZoomControls(QWidget *owner, const Icons &icons,
    const std::function<void(qreal)> &zoom, const std::function<void()> &custom);
qreal sliderToScale(int position);
int scaleToSlider(qreal scale);
qreal steppedScale(qreal scale, int direction, qreal step);
bool isSyncClick(Qt::KeyboardModifiers modifiers);
}
