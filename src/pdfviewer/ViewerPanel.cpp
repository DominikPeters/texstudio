#include "ViewerPanel.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

ViewerPanel::ViewerPanel(QSettings *settings, QWidget *parent) : QWidget(parent), settings(settings)
{
    setObjectName("previewPanel");
    setMinimumWidth(280);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto header = new QHBoxLayout;
    header->setContentsMargins(6, 3, 6, 3);
    header->addWidget(new QLabel(tr("Preview"), this)); header->addStretch();
    auto group = new QButtonGroup(this);
    const auto button = [&](const QString &text, const char *name, Mode mode) {
        auto b = new QToolButton(this); b->setText(text); b->setObjectName(name);
        b->setCheckable(true); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        group->addButton(b); header->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, mode] { setMode(mode); });
        return b;
    };
    liveButton = button(tr("Live"), "previewModeLive", Live);
    liveButton->setToolTip(tr("Preview current edits with TeXpresso"));
    pdfButton = button(tr("PDF"), "previewModePdf", Pdf);
    pdfButton->setToolTip(tr("View the last compiled PDF"));
    layout->addLayout(header);
    stack = new QStackedWidget(this); stack->setObjectName("previewStack"); layout->addWidget(stack, 1);
    emptyPdf = new QWidget(stack);
    auto emptyLayout = new QVBoxLayout(emptyPdf); emptyLayout->addStretch();
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
    live = viewer; stack->addWidget(viewer); refresh();
}
void ViewerPanel::setPdfSupported(bool supported)
{
    pdfSupported = supported; pdfButton->setEnabled(supported);
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
        stack->addWidget(viewer);
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
    liveButton->setChecked(currentMode == Live); pdfButton->setChecked(currentMode == Pdf);
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
