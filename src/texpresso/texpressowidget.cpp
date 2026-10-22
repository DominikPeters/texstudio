#include "texpressowidget.h"
#include "ViewerLinks.h"
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
    setMouseTracking(true);
    magnifier = new PDFMagnifier(this, 96, config,
        [this](int, QObject *, qreal resolution, const QRect &region) {
            if (region.isEmpty() || qint64(region.width()) * region.height() > 16000000) return QPixmap();
            lensRegion = region; lensResolution = resolution;
            if (preview) {
                const qreal factor = resolution / 72;
                const QRectF bounds(pageBounds.topLeft() + QPointF(region.topLeft()) / factor, QSizeF(region.size()) / factor);
                // Return the enlarged page immediately. A native cropped render
                // replaces only the lens once its latest request finishes.
                preview->requestRegion(bounds, qRound(bounds.intersected(pageBounds).width() * factor), ++lensToken);
            }
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
    setToolTip(QString()); setCursor(tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
    if (underMouse()) updateLinkCursor(mapFromGlobal(QCursor::pos()), QApplication::keyboardModifiers());
}
void TexpressoCanvas::setAnnotations(int page, qint64 revision, qint64 sequence, const QJsonObject &value)
{
    if (page == annotationPage && revision == annotationRevision && sequence == annotationSequence && annotations == value) return;
    resetTools();
    annotationPage = page; annotationRevision = revision; annotationSequence = sequence; annotations = value;
    if (underMouse()) updateLinkCursor(mapFromGlobal(QCursor::pos()), QApplication::keyboardModifiers());
}
int TexpressoCanvas::linkAt(const QPoint &point) const
{
    if (!fresh || pixels.isNull() || annotationPage < 0 || scale <= 0 || !rect().contains(point)) return -1;
    const QPointF pagePoint = pageBounds.topLeft() + QPointF(point) / scale;
    const auto links = annotations.value("links").toArray();
    for (int i = 0; i < links.size(); ++i) {
        const auto link = links[i].toObject();
        const auto bounds = link.value("bounds").toArray();
        if (bounds.size() != 4) continue;
        const QRectF rectangle(QPointF(bounds[0].toDouble(), bounds[1].toDouble()), QPointF(bounds[2].toDouble(), bounds[3].toDouble()));
        if (!rectangle.normalized().contains(pagePoint)) continue;
        const auto uri = link.value("uri").toString();
        if ((uri.startsWith('#') && uri.size() > 1) || ViewerLinks::externalUrl(uri)) return i;
    }
    return -1;
}
void TexpressoCanvas::updateLinkCursor(const QPoint &point, Qt::KeyboardModifiers modifiers)
{
    const int link = modifiers == Qt::NoModifier ? linkAt(point) : -1;
    setToolTip(link < 0 ? QString() : annotations.value("links").toArray()[link].toObject().value("uri").toString());
    if (!dragging) setCursor(link < 0 ? (tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor) : Qt::PointingHandCursor);
}
void TexpressoCanvas::resetTools()
{
    ++lensToken;
    if (preview) preview->cancelRegion();
    interactionActive = false; dragging = false; pressedLink = -1; linkDragged = false;
    setToolTip(QString()); magnifier->hide(); magnifier->invalidate(); setTool(tool);
}
void TexpressoCanvas::setPreview(TexpressoPreview *value)
{
    if (preview == value) return;
    resetTools(); disconnect(regionConnection); preview = value;
    if (!preview) return;
    regionConnection = connect(preview, &TexpressoPreview::regionReady, this,
        [this](qint64 token, const QImage &image, const QRectF &bounds) {
            if (token != lensToken || !fresh || !magnifier->isVisible() || lensRegion.isEmpty()) return;
            QPixmap detail(lensRegion.size()); detail.fill(Qt::white);
            QPainter painter(&detail); painter.setRenderHint(QPainter::SmoothPixmapTransform);
            const qreal factor = lensResolution / 72;
            painter.drawImage(QRectF((bounds.topLeft() - pageBounds.topLeft()) * factor - QPointF(lensRegion.topLeft()),
                bounds.size() * factor), image);
            painter.end(); magnifier->setImage(detail, 0);
        });
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
    linkGesture = false;
    interactionActive = true;
    setFocus(); pressedModifiers = event->modifiers();
    if (pressedModifiers == Qt::NoModifier && (pressedLink = linkAt(event->pos())) >= 0) {
        linkGesture = true; linkPressPosition = event->pos(); linkDragged = false; event->accept(); return;
    }
    if (pressedModifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::ShiftModifier | Qt::AltModifier)) return;
    emit activated();
    if (tool == 2) { dragging = true; dragPosition = event->globalPos(); setCursor(Qt::ClosedHandCursor); }
    else magnify(event->pos());
    event->accept();
}
void TexpressoCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (pressedLink >= 0) {
        if ((event->pos() - linkPressPosition).manhattanLength() >= QApplication::startDragDistance()) linkDragged = true;
        updateLinkCursor(event->pos(), event->modifiers());
        event->accept(); return;
    }
    if (dragging) {
        const QPoint delta = event->globalPos() - dragPosition; dragPosition = event->globalPos();
        area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() - delta.x());
        area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() - delta.y());
    } else if (magnifier->isVisible()) magnify(event->pos());
    else updateLinkCursor(event->pos(), event->modifiers());
    event->accept();
}
void TexpressoCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    const int link = pressedLink; const bool wasLinkGesture = linkGesture;
    const bool followLink = link >= 0 && !linkDragged && fresh && event->modifiers() == Qt::NoModifier && linkAt(event->pos()) == link;
    const int page = annotationPage; const qint64 revision = annotationRevision, sequence = annotationSequence;
    interactionActive = false;
    resetTools();
    linkGesture = false;
    if (followLink) emit linkActivated(page, link, revision, sequence);
    else if (wasLinkGesture) { /* A cancelled link gesture never turns into a tool gesture. */ }
    else if (fresh && (ViewerControls::isSyncClick(pressedModifiers) || ((pressedModifiers & Qt::MetaModifier) && !(pressedModifiers & Qt::ShiftModifier)))
        && (ViewerControls::isSyncClick(event->modifiers()) || ((event->modifiers() & Qt::MetaModifier) && !(event->modifiers() & Qt::ShiftModifier)))) {
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
    updateLinkCursor(event->pos(), event->modifiers());
    emit interactionFinished();
    event->accept();
}
void TexpressoCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && linkAt(event->pos()) < 0) magnify(event->pos());
}
void TexpressoCanvas::leaveEvent(QEvent *event)
{
    setToolTip(QString()); if (!dragging) setCursor(tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor); QWidget::leaveEvent(event);
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
    auto output = new QToolButton(this); output->setText(tr("Output")); output->setObjectName("texpressoOutput"); controls->addWidget(output);
    auto sync = new QToolButton(this); sync->setText(tr("Sync")); sync->setToolTip(tr("Show the editor cursor in the preview")); controls->addWidget(sync);
    auto follow = new QToolButton(this); follow->setDefaultAction(actions["followCursor"]); controls->addWidget(follow);
    controls->addStretch(); layout->addLayout(controls);
    auto reruns = new QHBoxLayout;
    auto autoRerun = new QAction(tr("Automatic reruns"), this);
    autoRerun->setCheckable(true); autoRerun->setChecked(true);
    autoRerun->setToolTip(tr("Run additional TeX passes after editing pauses to settle references and auxiliary files"));
    actions.insert("autoRerun", autoRerun);
    auto autoButton = new QToolButton(this); autoButton->setDefaultAction(autoRerun); reruns->addWidget(autoButton);
    auto rerunOnce = new QAction(tr("Rerun Once"), this); actions.insert("rerunOnce", rerunOnce);
    rerunOnce->setToolTip(tr("Run one additional TeX pass for references and auxiliary files. Use Build & Refresh to run BibTeX or Biber."));
    auto onceButton = new QToolButton(this); onceButton->setDefaultAction(rerunOnce); reruns->addWidget(onceButton);
    auto build = new QAction(tr("Build & Refresh"), this); actions.insert("build", build);
    build->setToolTip(tr("Run the normal compilation recipe, including bibliography tools, then refresh the live preview"));
    auto buildButton = new QToolButton(this); buildButton->setDefaultAction(build); reruns->addWidget(buildButton);
    reruns->addStretch(); layout->addLayout(reruns);
    connect(autoRerun, &QAction::toggled, this, [this](bool enabled) { if (shown) shown->setAutoRerun(enabled); });
    connect(rerunOnce, &QAction::triggered, this, [this] { if (shown) shown->rerunOnce(); });
    connect(build, &QAction::triggered, this, &TexpressoWidget::buildRequested);
    status = new QLabel(this); status->setObjectName("texpressoStatus"); status->setWordWrap(true); layout->addWidget(status);
    area = new QScrollArea(this); area->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    area->setWidgetResizable(false); area->setBackgroundRole(QPalette::Dark); area->setFrameStyle(QFrame::NoFrame);
    documentCanvas = new QWidget; documentCanvas->setObjectName("texpressoPages");
    canvas = new TexpressoCanvas(config, area); canvas->setParent(documentCanvas);
    canvas->setObjectName("texpressoActivePage"); area->setWidget(documentCanvas); layout->addWidget(area, 1);
    setFocusProxy(canvas);
    auto continuousAction = new QAction(tr("Continuous scrolling"), this);
    continuousAction->setCheckable(true); continuousAction->setChecked(true); actions.insert("continuous", continuousAction);
    toolbar->addAction(continuousAction);
    connect(continuousAction, &QAction::toggled, this, [this](bool enabled) {
        continuous = enabled; extentPages = 1; layoutPage(); showPage(shown ? shown->preview.page() : 0); updateViewport();
    });
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
        if (session != shown || !session || !session->preview.valid() || session->userStopped || session->failed() || session->externalBuildRunning()) {
            canvas->fresh = false; canvas->resetTools();
            for (auto page : extraCanvases) { page->fresh = false; page->resetTools(); }
        }
        if (!refreshTimer.isActive()) refreshTimer.start();
    });
    const auto navigate = [this](const QString &id, const std::function<int(TexpressoPreview &)> &target) {
        connect(actions[id], &QAction::triggered, this, [this, target] { if (shown) { shown->preview.navigateTo(target(shown->preview)); showPage(shown->preview.page()); } });
    };
    navigate("firstPage", [](TexpressoPreview &) { return 0; });
    navigate("previous", [](TexpressoPreview &p) { return p.page() - 1; });
    navigate("next", [](TexpressoPreview &p) { return p.page() + 1; });
    navigate("last", [](TexpressoPreview &p) { return p.pageCount() - 1; });
    connect(pages.number, &QLineEdit::returnPressed, this, [this] { if (shown) { shown->preview.navigateTo(pages.number->text().toInt() - 1); showPage(shown->preview.page()); } });
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
        const int tool = QString(id) == "scroll" ? 2 : 1; canvas->setTool(tool);
        for (auto page : extraCanvases) page->setTool(tool); emit toolChanged(tool);
    });
    connect(actions["followCursor"], &QAction::toggled, this, &TexpressoWidget::followCursorChanged);
    connect(pause, &QToolButton::clicked, this, [this] { if (shown) shown->setPaused(!shown->userPaused); });
    connect(stop, &QToolButton::clicked, this, [this] { if (shown) shown->stop(); });
    connect(restart, &QToolButton::clicked, manager, &TexpressoManager::restart);
    connect(sync, &QToolButton::clicked, this, &TexpressoWidget::syncRequested);
    connect(actions["gotoSource"], &QAction::triggered, this, [this] {
        if (shown && canvas->fresh) shown->preview.inverse(QPointF(canvas->pageBounds.center()));
    });
    connectCanvas(canvas);
    connect(output, &QToolButton::clicked, this, &TexpressoWidget::outputRequested);
    const auto saveScroll = [this] {
        if (shown && !updating) {
            shown->preview.scroll = {area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
            updateViewport(true);
        }
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
    // Keep the canvas under the mouse stable until release. Selecting a second
    // visible page must not recycle that widget halfway through a lens gesture.
    if (manager->current() == shown) {
        if (canvas->interactionActive) return;
        for (auto page : extraCanvases) if (page->interactionActive) return;
    }
    updating = true;
    auto session = manager->current();
    const bool switched = session != shown;
    shown = session;
    if (switched) {
        knownBounds.clear(); extentPages = 1; viewportPage = session ? session->preview.page() : 0;
        canvas->pixels = {}; canvas->setProperty("texpressoPage", -1);
        for (auto page : extraCanvases) { page->hide(); page->pixels = {}; page->setProperty("texpressoPage", -1); }
        disconnect(externalLinkConnection);
        if (session) externalLinkConnection = connect(&session->preview, &TexpressoPreview::externalLinkRequested,
            this, [this](const QUrl &url) { ViewerLinks::openUrl(url, this); });
    }
    canvas->setPreview(session ? &session->preview : nullptr);
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
    for (auto page : extraCanvases) page->setTool(config->editTool);
    actions[config->editTool == 2 ? "scroll" : "magnify"]->setChecked(true);
    if (session) {
        { const QSignalBlocker blocker(actions["autoRerun"]); actions["autoRerun"]->setChecked(session->autoRerun()); }
        actions["autoRerun"]->setEnabled(!session->userStopped && !session->failed());
        actions["rerunOnce"]->setEnabled(!session->userStopped && !session->userPaused && !session->failed() && !session->externalBuildRunning());
        actions["build"]->setEnabled(!session->externalBuildRunning());
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
        status->setText(session->failed() || session->userStopped || session->userPaused || session->externalBuildRunning() ? session->status
            : !session->compilerError.isEmpty() ? tr("TeX error: %1 — open Output for details.").arg(session->compilerError.section('\n', 0, 0))
            : !session->previewError.isEmpty() ? session->previewError
            : !session->compilerWarning.isEmpty() ? session->compilerWarning
            : preview.valid() ? liveHint : tr("Updating preview…"));
        status->setToolTip(QStringList{session->compilerError, session->compilerWarning, session->previewError}.join('\n').trimmed());
        updateCanvas(canvas, preview.page());
        shownInvert = config->invertColors; shownGray = config->grayscale;
        actions["gotoSource"]->setEnabled(canvas->fresh);
        if (switched) {
            disconnect(revealConnection);
            revealConnection = connect(&preview, &TexpressoPreview::reveal, this, [this, session](const QPointF &point) {
                if (shown != session) return;
                revealPoint(point, session->preview.revealingSource());
            });
        }
        layoutPage();
        if (!switched && viewportPage != preview.page() && !preview.followingSource()) {
            const QRect viewport(documentCanvas->mapFrom(area->viewport(), QPoint()), area->viewport()->size());
            if (preview.page() >= pageRects.size() || !pageRects[preview.page()].intersects(viewport)) showPage(preview.page());
        }
        if (switched) {
            area->horizontalScrollBar()->setValue(preview.scroll.x());
            area->verticalScrollBar()->setValue(preview.scroll.y());
        }
    } else {
        status->setText(tr("Open a saved LaTeX project to start live preview."));
        status->setToolTip(QString());
        canvas->pixels = {}; canvas->fresh = false; canvas->setAnnotations(-1, -1, -1, {}); canvas->hide();
        for (auto page : extraCanvases) { page->hide(); page->pixels = {}; page->fresh = false; page->setPreview(nullptr); page->setAnnotations(-1, -1, -1, {}); }
        documentCanvas->resize(1, 1); pages.count->clear();
    }
    updating = false;
    if (shown) updateViewport();
}
void TexpressoWidget::updateCanvas(TexpressoCanvas *pageCanvas, int page)
{
    auto &preview = shown->preview;
    QImage image = preview.pageImage(page);
    QRectF bounds = preview.boundsForPage(page);
    const bool samePage = pageCanvas->property("texpressoPage").isValid()
        && pageCanvas->property("texpressoPage").toInt() == page;
    // Invalidating a revision or changing raster width must disable interaction
    // immediately, but keep the last pixels for this page until its replacement
    // arrives. Never carry pixels over to a different page or project.
    if (image.isNull() && samePage) { image = pageCanvas->pixels; bounds = pageCanvas->pageBounds; }
    if (image.isNull()) {
        for (auto other : extraCanvases) {
            if (other->property("texpressoPage").toInt() != page || other->pixels.isNull()) continue;
            image = other->pixels; bounds = other->pageBounds; break;
        }
    }
    const bool changed = pageCanvas->pixels.cacheKey() != image.cacheKey()
        || shownInvert != config->invertColors || shownGray != config->grayscale;
    if (changed || !samePage) { pageCanvas->resetTools(); pageCanvas->invalidateMagnifier(); }
    pageCanvas->pixels = image; pageCanvas->pageBounds = bounds;
    pageCanvas->setProperty("texpressoPage", page);
    pageCanvas->fresh = preview.pageValid(page) && !shown->userStopped && !shown->failed() && !shown->externalBuildRunning();
    pageCanvas->setAnnotations(page, preview.revision(), preview.sequence(), preview.annotationsForPage(page));
    if (!pageCanvas->fresh) pageCanvas->resetTools();
    if (changed) pageCanvas->update();
}
void TexpressoWidget::connectCanvas(TexpressoCanvas *pageCanvas)
{
    connect(pageCanvas, &TexpressoCanvas::linkActivated, this, [this](int page, int index, qint64 revision, qint64 sequence) {
        if (shown) shown->preview.activateLink(page, index, revision, sequence);
    });
    connect(pageCanvas, &TexpressoCanvas::interactionFinished, this, [this] { if (!refreshTimer.isActive()) refreshTimer.start(); });
    pageCanvas->setContextMenuPolicy(Qt::ActionsContextMenu);
    for (const auto &id : {"magnify", "scroll", "zoomIn", "zoomOut", "gotoSource"}) pageCanvas->addAction(actions[id]);
    connect(pageCanvas, &TexpressoCanvas::activated, this, [this, pageCanvas] {
        if (!shown) return;
        const int page = pageCanvas->property("texpressoPage").toInt();
        viewportPage = page;
        const auto scroll = shown->preview.scroll; shown->preview.navigateTo(page); shown->preview.scroll = scroll;
    });
    connect(pageCanvas, &TexpressoCanvas::sourceClicked, this, [this, pageCanvas](const QPointF &point) {
        if (!shown) return;
        const int page = pageCanvas->property("texpressoPage").toInt();
        const auto scroll = shown->preview.scroll; shown->preview.setPage(page); shown->preview.scroll = scroll;
        shown->preview.inverse(point);
    });
    connect(pageCanvas, &TexpressoCanvas::zoomRequested, this, [this, pageCanvas](int direction, const QPoint &anchor) {
        setZoom(ViewerControls::steppedScale(pageCanvas->scale * 72 / 96, direction, config->zoomStepFactor), pageCanvas->mapTo(canvas, anchor));
    });
}
void TexpressoWidget::showPage(int page)
{
    if (!shown) return;
    const bool before = updating; updating = true;
    updateCanvas(canvas, shown->preview.page());
    layoutPage(); viewportPage = page;
    const QRect bounds = page < pageRects.size() ? pageRects[page] : QRect();
    if (!bounds.isEmpty()) area->verticalScrollBar()->setValue(bounds.top());
    updating = before;
    if (!updating) updateViewport();
}
void TexpressoWidget::revealPoint(const QPointF &point, bool source)
{
    if (!shown || !shown->preview.valid() || shown->userStopped || shown->failed() || shown->externalBuildRunning()
        || !std::isfinite(point.x()) || !std::isfinite(point.y())) return;
    auto &preview = shown->preview;
    const bool before = updating; updating = true;
    // The controller publishes the destination frame before revealing it; the
    // coalesced refresh may still hold another page's pixels and dimensions.
    canvas->resetTools();
    canvas->pixels = preview.image(); canvas->pageBounds = preview.bounds(); canvas->fresh = true;
    canvas->setAnnotations(preview.page(), preview.revision(), preview.sequence(), preview.annotationsForPage(preview.page()));
    canvas->invalidateMagnifier();
    layoutPage(); viewportPage = preview.page();
    const QPointF target = QPointF(canvas->pos()) + (point - canvas->pageBounds.topLeft()) * canvas->scale;
    if (source) {
        // Match the extension's comfortable region and small hysteresis band.
        // Each axis is independent, so following a line does not disturb an
        // already-visible horizontal position.
        const auto revealAxis = [](QScrollBar *bar, qreal value, int size) {
            const qreal margin = std::min(48.0, size * .1);
            const qreal start = bar->value();
            if (value >= start + margin - 4 && value <= start + size - margin + 4) return;
            const int centered = std::clamp(qRound(value - size / 2.0), bar->minimum(), bar->maximum());
            if (std::abs(centered - bar->value()) > 4) bar->setValue(centered);
        };
        revealAxis(area->horizontalScrollBar(), target.x(), area->viewport()->width());
        revealAxis(area->verticalScrollBar(), target.y(), area->viewport()->height());
    } else {
        // An explicit hyperlink keeps the ordinary destination navigation.
        if (preview.page() < pageRects.size()) area->verticalScrollBar()->setValue(pageRects[preview.page()].top());
        area->ensureVisible(qRound(target.x()), qRound(target.y()), 40, 60);
    }
    preview.scroll = {area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
    canvas->update(); updating = before;
    if (!updating) updateViewport();
}
void TexpressoWidget::setZoom(qreal value, const QPoint &anchor)
{
    if (!shown) return;
    const QPoint viewportPosition = anchor.x() < 0 ? area->viewport()->rect().center() : canvas->mapTo(area->viewport(), anchor);
    const QPoint documentPoint = documentCanvas->mapFrom(area->viewport(), viewportPosition);
    int page = shown->preview.page();
    for (int i = 0; i < pageRects.size(); ++i) if (pageRects[i].contains(documentPoint)) { page = i; break; }
    const auto oldRect = page < pageRects.size() ? pageRects[page] : QRect();
    const QPointF fraction = oldRect.isEmpty() ? QPointF() : QPointF(double(documentPoint.x() - oldRect.x()) / oldRect.width(), double(documentPoint.y() - oldRect.y()) / oldRect.height());
    shown->preview.zoom = value; canvas->resetTools();
    const bool before = updating; updating = true; layoutPage();
    if (page < pageRects.size()) {
        const auto target = pageRects[page];
        area->horizontalScrollBar()->setValue(qRound(target.x() + fraction.x() * target.width()) - viewportPosition.x());
        area->verticalScrollBar()->setValue(qRound(target.y() + fraction.y() * target.height()) - viewportPosition.y());
    }
    updating = before; updateViewport(true);
}
void TexpressoWidget::layoutPage()
{
    if (!shown || canvas->pageBounds.isEmpty()) { canvas->hide(); return; }
    auto &preview = shown->preview;
    if (preview.pageValid(preview.page())) knownBounds.insert(preview.page(), canvas->pageBounds);
    const auto bounds = canvas->pageBounds;
    const double widthScale = std::max(0.05, (area->viewport()->width() - 16.0) / bounds.width());
    canvas->scale = preview.zoom == 0 ? widthScale : preview.zoom < 0
        ? std::min(widthScale, std::max(0.05, (area->viewport()->height() - 16.0) / bounds.height())) : preview.zoom * 96.0 / 72;
    const int count = continuous ? std::clamp(std::max(preview.page() + 1, preview.pageCount() + (preview.complete() ? 0 : 1)), 1, 10000) : 1;
    extentPages = preview.complete() ? count : std::max(extentPages, count);
    if (!continuous) extentPages = 1;
    // Geometry metadata is cheap; only intersecting pages own canvases/pixels.
    // Preserve the top-page fraction as page dimensions become known.
    const int oldY = area->verticalScrollBar()->value();
    int anchorPage = -1; double anchorFraction = 0;
    for (int i = 0; i < pageRects.size(); ++i) if (pageRects[i].bottom() >= oldY) {
        anchorPage = i; anchorFraction = double(oldY - pageRects[i].top()) / std::max(1, pageRects[i].height()); break;
    }
    pageRects.clear(); int y = 8, width = 1;
    const int geometryCount = continuous ? extentPages : preview.page() + 1;
    for (int i = 0; i < geometryCount; ++i) {
        const auto pageBounds = knownBounds.value(i, bounds);
        QSize size(std::max(1, qRound(pageBounds.width() * canvas->scale)), std::max(1, qRound(pageBounds.height() * canvas->scale)));
        if (!continuous && i != preview.page()) { pageRects.append(QRect()); continue; }
        pageRects.append(QRect(QPoint(8, y), size)); y += size.height() + 12; width = std::max(width, size.width() + 16);
        if (y > 16000000) { extentPages = i + 1; break; }
    }
    const bool before = updating; updating = true;
    documentCanvas->resize(width, std::min(y, 16000000));
    if (anchorPage >= 0 && anchorPage < pageRects.size() && !pageRects[anchorPage].isEmpty())
        area->verticalScrollBar()->setValue(qRound(pageRects[anchorPage].top() + anchorFraction * pageRects[anchorPage].height()));
    updating = before;
    const int activePage = preview.page();
    canvas->setProperty("texpressoPage", activePage);
    if (activePage < pageRects.size()) canvas->setGeometry(pageRects[activePage]);
    canvas->show();
    const qreal factor = canvas->scale * 72 / 96;
    zoomControls.scale->setText(QString::number(qRound(factor * 100)) + '%');
    { const QSignalBlocker block(zoomControls.slider); zoomControls.slider->setValue(ViewerControls::scaleToSlider(factor)); }
    actions["fitToWidth"]->setChecked(preview.zoom == 0); actions["fitToWindow"]->setChecked(preview.zoom < 0);
    actions["zoomIn"]->setEnabled(factor < 4); zoomControls.in->setEnabled(factor < 4);
    actions["zoomOut"]->setEnabled(factor > .125); zoomControls.out->setEnabled(factor > .125);
    const int widthPixels = qRound(canvas->width() * devicePixelRatioF());
    if (shown != rasterSession || widthPixels != rasterWidth) {
        rasterSession = shown; rasterWidth = widthPixels; widthTimer.start();
    }
}
void TexpressoWidget::updateViewport(bool gesture)
{
    if (!shown || updating || pageRects.isEmpty()) return;
    auto &preview = shown->preview;
    const QRect viewport(documentCanvas->mapFrom(area->viewport(), QPoint()), area->viewport()->size());
    QList<int> visible; int primary = preview.page(), greatest = 0;
    for (int i = 0; i < pageRects.size(); ++i) {
        const int overlap = pageRects[i].intersected(viewport).height();
        if (overlap <= 0 || pageRects[i].isEmpty()) continue;
        visible.append(i);
        if (overlap > greatest) { primary = i; greatest = overlap; }
    }
    if (visible.isEmpty()) visible.append(primary);
    visible.removeAll(primary); visible.prepend(primary);
    viewportPage = primary;
    const int previousPage = canvas->property("texpressoPage").toInt();
    const QImage previousPixels = canvas->pixels;
    const QRectF previousBounds = canvas->pageBounds;
    if (continuous) preview.setViewportPages(visible, gesture);
    else preview.setViewportPages({preview.page()}, gesture);
    // The controller may select a new primary page synchronously. Move its
    // pixels and geometry together, before returning to the event loop.
    updateCanvas(canvas, preview.page());
    if (preview.page() < pageRects.size()) canvas->setGeometry(pageRects[preview.page()]);
    canvas->show();
    int slot = 0; bool geometryChanged = false;
    const QList<int> ordered = continuous ? visible : QList<int>{};
    for (int page : ordered) {
        if (page == preview.page() || slot >= 15) continue;
        if (slot == extraCanvases.size()) {
            auto extra = new TexpressoCanvas(config, area); extra->setParent(documentCanvas); extra->setObjectName("texpressoNeighborPage");
            connectCanvas(extra); extraCanvases.append(extra);
        }
        // Keep a canvas attached to its page while it remains visible. Slots
        // otherwise swap at a page boundary and discard useful stale pixels.
        for (int i = slot; i < extraCanvases.size(); ++i) {
            if (extraCanvases[i]->property("texpressoPage").toInt() == page) { extraCanvases.swapItemsAt(slot, i); break; }
        }
        auto extra = extraCanvases[slot++];
        extra->setPreview(&preview); extra->scale = canvas->scale;
        if (page == previousPage && preview.pageImage(page).isNull()) {
            extra->pixels = previousPixels; extra->pageBounds = previousBounds; extra->setProperty("texpressoPage", page);
        }
        updateCanvas(extra, page);
        if (preview.pageValid(page) && knownBounds.value(page) != extra->pageBounds) {
            knownBounds.insert(page, extra->pageBounds); geometryChanged = true;
        }
        extra->setTool(config->editTool); extra->setGeometry(pageRects[page]); extra->show(); extra->update();
    }
    for (; slot < extraCanvases.size(); ++slot) { extraCanvases[slot]->hide(); extraCanvases[slot]->pixels = {}; extraCanvases[slot]->fresh = false; extraCanvases[slot]->resetTools(); extraCanvases[slot]->setAnnotations(-1, -1, -1, {}); }
    if (geometryChanged && !refreshTimer.isActive()) refreshTimer.start();
}
void TexpressoWidget::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); layoutPage(); }
