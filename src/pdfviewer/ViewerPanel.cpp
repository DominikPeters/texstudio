#include "ViewerPanel.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCryptographicHash>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QToolBar>
#include <QVBoxLayout>

ViewerPanel::ViewerPanel(QSettings *settings, QWidget *parent) : QWidget(parent), settings(settings)
{
    setObjectName("previewPanel");
    setMinimumWidth(280);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    // The mode switch leads each viewer's own toolbar instead of taking a row.
    auto group = new QActionGroup(this);
    const auto action = [&](const QString &text, const char *name, Mode mode) {
        auto a = new QAction(text, this); a->setObjectName(name); a->setCheckable(true);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode] { setMode(mode); });
        return a;
    };
    liveAction = action(tr("Live"), "previewModeLive", Live);
    liveAction->setToolTip(tr("Preview current edits with TeXpresso"));
    pdfAction = action(tr("PDF"), "previewModePdf", Pdf);
    pdfAction->setToolTip(tr("View the last compiled PDF"));
    stack = new QStackedWidget(this); stack->setObjectName("previewStack"); layout->addWidget(stack, 1);
    emptyPdf = new QWidget(stack);
    auto emptyLayout = new QVBoxLayout(emptyPdf); emptyLayout->setContentsMargins(0, 0, 0, 0);
    auto emptyToolbar = new QToolBar(emptyPdf); emptyToolbar->setObjectName("previewEmptyToolbar");
    emptyToolbar->setIconSize(QSize(16, 16)); emptyLayout->addWidget(emptyToolbar);
    addModeActions(emptyPdf); emptyLayout->addStretch();
    auto message = new QLabel(tr("No compiled PDF is available for this project yet."), emptyPdf);
    message->setWordWrap(true); message->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(message);
    auto build = new QPushButton(tr("Build PDF"), emptyPdf); build->setObjectName("previewBuildPdf");
    emptyLayout->addWidget(build, 0, Qt::AlignHCenter); emptyLayout->addStretch();
    connect(build, &QPushButton::clicked, this, &ViewerPanel::buildRequested);
    stack->addWidget(emptyPdf);
    refresh();
}
QString ViewerPanel::settingsKey() const
{
    return "preview/projects/" + QString::fromLatin1(QCryptographicHash::hash(currentProject.toUtf8(), QCryptographicHash::Sha256).toHex()) + "/mode";
}
ViewerPanel::~ViewerPanel()
{
    // A newer view may have replaced a project's mapping while its older
    // widget is still owned by the stack (for example a cloned PDF).
    for (int i = 0; i < stack->count(); ++i) disconnect(stack->widget(i), nullptr, this, nullptr);
}
void ViewerPanel::setLiveViewer(QWidget *viewer)
{
    live = viewer; stack->addWidget(viewer); addModeActions(viewer); refresh();
}
void ViewerPanel::setPdfSupported(bool supported)
{
    // Without a PDF viewer a lone Live switch would be inert.
    pdfSupported = supported; liveAction->setVisible(supported); pdfAction->setVisible(supported);
    if (!supported) currentMode = Live;
    refresh();
}
void ViewerPanel::setProject(const QString &project)
{
    if (currentProject == project) return;
    currentProject = project;
    currentMode = pdfSupported && settings && settings->value(settingsKey(), "live").toString() == "pdf" ? Pdf : Live;
    refresh(true);
}
void ViewerPanel::setMode(Mode mode)
{
    if (mode == Pdf && !pdfSupported) return;
    const bool changed = currentMode != mode;
    currentMode = mode;
    if (settings && !currentProject.isEmpty()) settings->setValue(settingsKey(), mode == Pdf ? "pdf" : "live");
    refresh(true);
    if (changed) emit modeChanged(mode);
}
void ViewerPanel::addModeActions(QWidget *viewer)
{
    auto toolbar = viewer->findChild<QToolBar *>(QString(), Qt::FindDirectChildrenOnly);
    if (!toolbar || toolbar->actions().contains(liveAction)) return;
    const auto first = toolbar->actions().value(0);
    toolbar->insertActions(first, {liveAction, pdfAction});
    if (first) toolbar->insertSeparator(first);
}
QWidget *ViewerPanel::pdf(const QString &project) const { return pdfs.value(project); }
QWidget *ViewerPanel::currentViewer() const { return stack->currentWidget(); }
bool ViewerPanel::liveVisible() const { return isVisible() && currentMode == Live && live && stack->currentWidget() == live; }
void ViewerPanel::addPdf(const QString &project, QWidget *viewer)
{
    for (auto it = pdfs.begin(); it != pdfs.end();) {
        if (!it.value() || it.value() == viewer) it = pdfs.erase(it); else ++it;
    }
    pdfs[project] = viewer;
    if (stack->indexOf(viewer) < 0) {
        stack->addWidget(viewer); addModeActions(viewer);
        connect(viewer, &QObject::destroyed, this, [this] { refresh(); });
    }
    refresh();
}
void ViewerPanel::refresh(bool requestPdf)
{
    auto target = currentMode == Live && live ? live.data() : pdf(currentProject);
    if (!target) target = emptyPdf;
    const auto previous = stack->currentWidget();
    const bool hadFocus = previous && (previous->hasFocus() || previous->isAncestorOf(QApplication::focusWidget()));
    stack->setCurrentWidget(target); setFocusProxy(target);
    liveAction->setChecked(currentMode == Live); pdfAction->setChecked(currentMode == Pdf);
    emptyPdf->findChild<QPushButton *>()->setEnabled(!currentProject.isEmpty() && pdfSupported);
    if (previous != target && hadFocus) target->setFocus();
    updateLiveVisibility();
    if (previous != target) emit currentViewerChanged(target);
    if (requestPdf && currentMode == Pdf && !pdf(currentProject) && !currentProject.isEmpty()) emit pdfRequested(currentProject);
}
void ViewerPanel::updateLiveVisibility()
{
    const bool visible = liveVisible();
    if (lastLiveVisible == visible) return;
    lastLiveVisible = visible; emit liveVisibilityChanged(visible);
}
void ViewerPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); updateLiveVisibility(); }
void ViewerPanel::hideEvent(QHideEvent *event) { QWidget::hideEvent(event); updateLiveVisibility(); }
