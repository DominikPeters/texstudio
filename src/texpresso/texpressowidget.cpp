#include "texpressowidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextEdit>
#include <QDialog>
#include <QInputDialog>
#include <QActionGroup>
#include <QSignalBlocker>
#include <QClipboard>
#include <QApplication>
#include <cmath>

TexpressoCanvas::TexpressoCanvas(PDFDocumentConfig *config, QScrollArea *area)
    : config(config), area(area)
{
    setFocusPolicy(Qt::StrongFocus);
    magnifier = new PDFMagnifier(this, 96, config,
        [this](int, QObject *, qreal resolution, const QRect &region) {
            // Until engine region rendering is connected, magnify the current
            // frame. The same lens can later consume a sharper engine region.
            if (region.isEmpty() || qint64(region.width()) * region.height() > 16000000) return QPixmap();
            QPixmap result(region.size()); result.fill(Qt::white);
            QPainter painter(&result); painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(-region.x(), -region.y(), pageBounds.width() * resolution / 72,
                pageBounds.height() * resolution / 72), pixels);
            return result;
        });
    magnifier->hide();
    setTool(config->editTool);
}
void TexpressoCanvas::setTool(int value)
{
    tool = value == 2 ? 2 : 1;
    setCursor(tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
}
void TexpressoCanvas::resetTools()
{
    dragging = false; magnifier->hide(); setTool(tool);
}
void TexpressoCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (pixels.isNull()) return;
    if (!config->invertColors && !config->grayscale) {
        convertedPixels = {}; convertedKey = 0;
        painter.drawImage(rect(), pixels);
    } else {
        if (convertedKey != pixels.cacheKey() || convertedInvert != config->invertColors || convertedGray != config->grayscale) {
            convertedPixels = convertImage(QPixmap::fromImage(pixels), config->invertColors, config->grayscale);
            convertedKey = pixels.cacheKey(); convertedInvert = config->invertColors; convertedGray = config->grayscale;
        }
        painter.drawPixmap(rect(), convertedPixels);
    }
}
void TexpressoCanvas::magnify(const QPoint &point)
{
    if (!fresh || pixels.isNull()) return;
    magnifier->setFixedSize(config->magnifierSize * 4 / 3, config->magnifierSize);
    magnifier->reshape();
    // Render only the lens neighborhood. A whole viewport can exceed the
    // allocation limit on large/high-DPI displays and leave an empty lens.
    const QSize sampleSize = magnifier->size() / 2 + QSize(32, 32);
    const QRect sample(point - QPoint(sampleSize.width() / 2, sampleSize.height() / 2), sampleSize);
    magnifier->setPage(0, scale * 72 / 96, rect(), sample.intersected(rect()));
    magnifier->move(point - magnifier->rect().center());
    magnifier->show(); magnifier->raise();
}
void TexpressoCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    setFocus(); pressedModifiers = event->modifiers();
    if (pressedModifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) return;
    if (tool == 2) { dragging = true; dragPosition = event->globalPos(); setCursor(Qt::ClosedHandCursor); }
    else magnify(event->pos());
    event->accept();
}
void TexpressoCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging) {
        const QPoint delta = event->globalPos() - dragPosition; dragPosition = event->globalPos();
        area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() - delta.x());
        area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() - delta.y());
    } else if (magnifier->isVisible()) magnify(event->pos());
    event->accept();
}
void TexpressoCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    resetTools();
    if (fresh && ViewerControls::isSyncClick(pressedModifiers) && ViewerControls::isSyncClick(event->modifiers())) {
        const auto point = event->pos();
        emit sourceClicked(QPointF(point.x() / scale + pageBounds.left(), point.y() / scale + pageBounds.top()));
    } else if ((pressedModifiers & Qt::ControlModifier) && (pressedModifiers & Qt::ShiftModifier)
        && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        const auto point = event->pos();
        const double cm = 2.54 / 72;
        QApplication::clipboard()->setText(QString("%1, %2").arg((point.x() / scale + pageBounds.left()) * cm)
            .arg((pageBounds.bottom() - point.y() / scale) * cm));
    } else if (tool == 1 && (event->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))) {
        emit zoomRequested(event->modifiers() & Qt::AltModifier ? -1 : 1, event->pos());
    }
    event->accept();
}
void TexpressoCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) magnify(event->pos());
}
void TexpressoCanvas::wheelEvent(QWheelEvent *event)
{
    if ((event->modifiers() & Qt::ControlModifier) && event->angleDelta().y()) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1, event->position().toPoint()); event->accept();
    } else event->ignore();
}
TexpressoWidget::TexpressoWidget(TexpressoManager *manager, PDFDocumentConfig *config,
    const ViewerControls::Icons &icons, QWidget *parent) : QWidget(parent), manager(manager), config(config)
{
    setObjectName("texpressoLivePreview"); setMinimumWidth(280);
    auto layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    project = new QLabel(this); project->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(project);
    toolbar = new QToolBar(this); toolbar->setObjectName("texpressoViewerToolbar"); toolbar->setIconSize(QSize(16, 16)); layout->addWidget(toolbar);
    for (const auto &id : {"magnify", "scroll", "firstPage", "previous", "next", "last", "actualSize", "fitToWidth", "fitToWindow", "zoomIn", "zoomOut", "gotoSource", "followCursor"})
        actions.insert(id, ViewerControls::createAction(id, this, icons));
    ViewerControls::addToolbarActions(toolbar, actions);
    pages = ViewerControls::insertPageControls(toolbar, actions["next"], false);
    auto tools = new QActionGroup(this); tools->addAction(actions["magnify"]); tools->addAction(actions["scroll"]);
    actions[config->editTool == 2 ? "scroll" : "magnify"]->setChecked(true);
    auto controls = new QHBoxLayout;
    pause = new QToolButton(this); pause->setText(tr("Pause")); controls->addWidget(pause);
    stop = new QToolButton(this); stop->setText(tr("Stop")); controls->addWidget(stop);
    restart = new QToolButton(this); restart->setText(tr("Restart")); controls->addWidget(restart);
    auto output = new QToolButton(this); output->setText(tr("Output")); controls->addWidget(output);
    auto sync = new QToolButton(this); sync->setText(tr("Sync")); sync->setToolTip(tr("Show the editor cursor in the preview")); controls->addWidget(sync);
    auto follow = new QToolButton(this); follow->setDefaultAction(actions["followCursor"]); controls->addWidget(follow);
    controls->addStretch(); layout->addLayout(controls);
    status = new QLabel(this); status->setWordWrap(true); layout->addWidget(status);
    area = new QScrollArea(this); area->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    area->setWidgetResizable(false); area->setBackgroundRole(QPalette::Dark); area->setFrameStyle(QFrame::NoFrame);
    canvas = new TexpressoCanvas(config, area); area->setWidget(canvas); layout->addWidget(area, 1);
    setFocusProxy(canvas);
    canvas->setContextMenuPolicy(Qt::ActionsContextMenu);
    for (const auto &id : {"magnify", "scroll", "zoomIn", "zoomOut", "gotoSource"}) canvas->addAction(actions[id]);
    zoomControls = ViewerControls::createZoomControls(this, icons, [this](qreal value) { setZoom(value); }, [this] {
        bool ok; const int value = QInputDialog::getInt(this, tr("Custom Zoom"), tr("Zoom Factor (%):"), 100, 13, 400, 10, &ok);
        if (ok) setZoom(value / 100.0);
    });
    auto zoomBar = new QHBoxLayout; zoomBar->addStretch();
    for (QWidget *widget : QList<QWidget *>{zoomControls.scale, zoomControls.out, zoomControls.slider, zoomControls.in}) zoomBar->addWidget(widget);
    layout->addLayout(zoomBar);
    canvas->hide();
    widthTimer.setSingleShot(true); widthTimer.setInterval(100);
    connect(&widthTimer, &QTimer::timeout, this, [this] {
        if (auto session = this->manager->current()) session->preview.setWidth(qRound(canvas->width() * devicePixelRatioF()));
    });
    refreshTimer.setSingleShot(true);
    connect(&refreshTimer, &QTimer::timeout, this, &TexpressoWidget::refresh);
    connect(manager, &TexpressoManager::updated, this, [this] {
        // Invalidate interaction immediately, but merge status/frame/layout
        // notifications from the same protocol batch into one GUI update.
        auto session = this->manager->current();
        if (session != shown || !session || !session->preview.valid() || session->userStopped || session->failed()) {
            canvas->fresh = false; canvas->resetTools();
        }
        if (!refreshTimer.isActive()) refreshTimer.start();
    });
    const auto navigate = [this](const QString &id, const std::function<int(TexpressoPreview &)> &target) {
        connect(actions[id], &QAction::triggered, this, [this, target] { if (shown) shown->preview.setPage(target(shown->preview)); });
    };
    navigate("firstPage", [](TexpressoPreview &) { return 0; });
    navigate("previous", [](TexpressoPreview &p) { return p.page() - 1; });
    navigate("next", [](TexpressoPreview &p) { return p.page() + 1; });
    navigate("last", [](TexpressoPreview &p) { return p.pageCount() - 1; });
    connect(pages.number, &QLineEdit::returnPressed, this, [this] { if (shown) shown->preview.setPage(pages.number->text().toInt() - 1); });
    connect(actions["actualSize"], &QAction::triggered, this, [this] { setZoom(1); });
    connect(actions["fitToWidth"], &QAction::triggered, this, [this] { setZoom(0); });
    connect(actions["fitToWindow"], &QAction::triggered, this, [this] { setZoom(-1); });
    for (const auto &id : {"zoomIn", "zoomOut"}) connect(actions[id], &QAction::triggered, this, [this, id] {
        setZoom(ViewerControls::steppedScale(canvas->scale * 72 / 96, QString(id) == "zoomIn" ? 1 : -1, this->config->zoomStepFactor));
    });
    connect(zoomControls.in, &QToolButton::clicked, actions["zoomIn"], &QAction::trigger);
    connect(zoomControls.out, &QToolButton::clicked, actions["zoomOut"], &QAction::trigger);
    connect(zoomControls.slider, &QSlider::valueChanged, this, [this](int value) { setZoom(ViewerControls::sliderToScale(value)); });
    for (const auto &id : {"magnify", "scroll"}) connect(actions[id], &QAction::triggered, this, [this, id] {
        const int tool = QString(id) == "scroll" ? 2 : 1; canvas->setTool(tool); emit toolChanged(tool);
    });
    connect(actions["followCursor"], &QAction::toggled, this, &TexpressoWidget::followCursorChanged);
    connect(pause, &QToolButton::clicked, this, [this] { if (shown) shown->setPaused(!shown->userPaused); });
    connect(stop, &QToolButton::clicked, this, [this] { if (shown) shown->stop(); });
    connect(restart, &QToolButton::clicked, manager, &TexpressoManager::restart);
    connect(sync, &QToolButton::clicked, this, &TexpressoWidget::syncRequested);
    connect(actions["gotoSource"], &QAction::triggered, this, [this] {
        if (shown && canvas->fresh) shown->preview.inverse(QPointF(canvas->pageBounds.center()));
    });
    connect(canvas, &TexpressoCanvas::sourceClicked, this, [this](const QPointF &point) { if (shown) shown->preview.inverse(point); });
    connect(canvas, &TexpressoCanvas::zoomRequested, this, [this](int direction, const QPoint &anchor) {
        setZoom(ViewerControls::steppedScale(canvas->scale * 72 / 96, direction, this->config->zoomStepFactor), anchor);
    });
    connect(output, &QToolButton::clicked, this, [this] {
        if (!shown) return;
        auto dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("TeXpresso output — %1").arg(QFileInfo(shown->root()).fileName()));
        auto layout = new QVBoxLayout(dialog); auto text = new QPlainTextEdit(dialog); text->setReadOnly(true);
        text->setPlainText(shown->compilerOutput + "\n\n" + shown->engineOutput); layout->addWidget(text); dialog->resize(800, 500); dialog->show();
    });
    const auto saveScroll = [this] {
        if (shown && !updating) shown->preview.scroll = {area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
    };
    connect(area->horizontalScrollBar(), &QScrollBar::valueChanged, this, saveScroll);
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, saveScroll);
    refresh();
}
void TexpressoWidget::configureAppearance(bool modern, int iconSize)
{
    toolbar->setIconSize(QSize(iconSize, iconSize));
    pages.number->setStyleSheet(modern ? "QLineEdit{ color: white; padding-top: -1px; margin-right: 2px; }"
                                      : "QLineEdit{ padding-top: -1px; margin-right: 2px; }");
}
void TexpressoWidget::refresh()
{
    refreshTimer.stop();
    updating = true;
    auto session = manager->current();
    const bool switched = session != shown;
    shown = session;
    if (switched) canvas->resetTools();
    const QFileInfo root(session ? session->root() : QString());
    project->setText(session ? tr("Live — %1 / %2").arg(root.dir().dirName(), root.fileName()) : tr("Live preview"));
    project->setToolTip(session ? session->root() : QString());
    for (auto button : {pause, stop, restart}) button->setEnabled(session != nullptr);
    for (auto action : actions) action->setEnabled(session != nullptr);
    pages.number->setEnabled(session); zoomControls.scale->setEnabled(session); zoomControls.slider->setEnabled(session);
    {
        const QSignalBlocker blocker(actions["followCursor"]);
        actions["followCursor"]->setChecked(config->followFromCursor);
    }
    canvas->setTool(config->editTool);
    actions[config->editTool == 2 ? "scroll" : "magnify"]->setChecked(true);
    if (session) {
        auto &preview = session->preview;
        pages.validator->setTop(preview.complete() ? std::max(1, preview.pageCount()) : 99999);
        if (!pages.number->hasFocus() || switched) pages.number->setText(QString::number(preview.page() + 1));
        pages.count->setText(preview.complete() ? QString::number(preview.pageCount()) : QString::fromUtf8("…"));
        for (const auto &id : {"firstPage", "previous"}) actions[id]->setEnabled(preview.page() > 0);
        actions["next"]->setEnabled(!preview.complete() || preview.page() + 1 < preview.pageCount());
        actions["last"]->setEnabled(preview.complete() && preview.pageCount() > 0 && preview.page() + 1 < preview.pageCount());
        pause->setText(session->userPaused ? tr("Resume") : tr("Pause"));
        pause->setEnabled(!session->userStopped && !session->failed());
        stop->setEnabled(!session->userStopped && !session->failed());
#ifdef Q_OS_MACOS
        const QString liveHint = tr("Live preview · Command-click to open source");
#else
        const QString liveHint = tr("Live preview · Ctrl-click to open source");
#endif
        status->setText(session->failed() || session->userStopped || session->userPaused ? session->status
            : !session->previewError.isEmpty() ? session->previewError
            : preview.valid() ? liveHint : tr("Updating preview…"));
        const bool changed = canvas->pixels.cacheKey() != preview.image().cacheKey()
            || shownInvert != config->invertColors || shownGray != config->grayscale;
        if (changed) { canvas->resetTools(); canvas->invalidateMagnifier(); }
        shownInvert = config->invertColors; shownGray = config->grayscale;
        canvas->pixels = preview.image(); canvas->pageBounds = preview.bounds(); canvas->fresh = preview.valid() && !session->userStopped && !session->failed();
        if (!canvas->fresh) canvas->resetTools();
        actions["gotoSource"]->setEnabled(canvas->fresh);
        if (switched) {
            disconnect(revealConnection);
            revealConnection = connect(&preview, &TexpressoPreview::reveal, this, [this, session](const QPointF &point) {
                if (shown != session) return;
                area->ensureVisible(qRound((point.x() - canvas->pageBounds.left()) * canvas->scale), qRound((point.y() - canvas->pageBounds.top()) * canvas->scale), 40, 60);
            });
        }
        layoutPage();
        if (changed) canvas->update();
        if (switched) {
            area->horizontalScrollBar()->setValue(preview.scroll.x());
            area->verticalScrollBar()->setValue(preview.scroll.y());
        }
    } else {
        status->setText(tr("Open a saved LaTeX project to start live preview."));
        canvas->pixels = {}; canvas->fresh = false; canvas->hide(); pages.count->clear();
    }
    updating = false;
}
void TexpressoWidget::setZoom(qreal value, const QPoint &anchor)
{
    if (!shown) return;
    const QPoint reference = anchor.x() < 0 ? canvas->mapFrom(area->viewport(), area->viewport()->rect().center()) : anchor;
    const QPoint viewportPosition = canvas->mapTo(area->viewport(), reference);
    const QPointF pagePoint = QPointF(reference) / canvas->scale;
    shown->preview.zoom = value; canvas->resetTools(); layoutPage();
    area->horizontalScrollBar()->setValue(qRound(pagePoint.x() * canvas->scale) - viewportPosition.x());
    area->verticalScrollBar()->setValue(qRound(pagePoint.y() * canvas->scale) - viewportPosition.y());
}
void TexpressoWidget::layoutPage()
{
    if (!shown || canvas->pixels.isNull() || canvas->pageBounds.isEmpty()) { canvas->hide(); return; }
    const auto bounds = canvas->pageBounds;
    const double widthScale = std::max(0.05, (area->viewport()->width() - 4.0) / bounds.width());
    canvas->scale = shown->preview.zoom == 0 ? widthScale : shown->preview.zoom < 0
        ? std::min(widthScale, std::max(0.05, (area->viewport()->height() - 4.0) / bounds.height())) : shown->preview.zoom * 96.0 / 72;
    canvas->resize(qRound(bounds.width() * canvas->scale), qRound(bounds.height() * canvas->scale));
    const qreal factor = canvas->scale * 72 / 96;
    zoomControls.scale->setText(QString::number(qRound(factor * 100)) + '%');
    { const QSignalBlocker block(zoomControls.slider); zoomControls.slider->setValue(ViewerControls::scaleToSlider(factor)); }
    actions["fitToWidth"]->setChecked(shown->preview.zoom == 0);
    actions["fitToWindow"]->setChecked(shown->preview.zoom < 0);
    actions["zoomIn"]->setEnabled(factor < 4); zoomControls.in->setEnabled(factor < 4);
    actions["zoomOut"]->setEnabled(factor > .125); zoomControls.out->setEnabled(factor > .125);
    canvas->show();
    const int width = qRound(canvas->width() * devicePixelRatioF());
    if (shown != rasterSession || width != rasterWidth) {
        rasterSession = shown; rasterWidth = width; widthTimer.start();
    }
}
void TexpressoWidget::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); layoutPage(); }
