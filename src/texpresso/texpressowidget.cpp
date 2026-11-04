#include "texpressowidget.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFrame>
#include "ViewerLinks.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QScrollBar>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextEdit>
#include <QDialog>
#include <QInputDialog>
#include <QActionGroup>
#include <QStatusBar>
#include <QTextDocument>
#include <QSignalBlocker>
#include <QClipboard>
#include <QApplication>
#include <cmath>

TexpressoCanvas::TexpressoCanvas(PDFDocumentConfig *config, QScrollArea *area)
    : config(config), area(area)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    figurePopup = new QFrame(this, Qt::ToolTip);
    figurePopup->setAttribute(Qt::WA_ShowWithoutActivating);
    figurePopup->setObjectName("texpressoTikzPopover");
    auto popupLayout = new QVBoxLayout(figurePopup);
    popupLayout->setContentsMargins(6, 4, 6, 4);
    auto editFigure = new QPushButton(tr("Edit in TikZ Editor"), figurePopup);
    editFigure->setObjectName("texpressoEditTikz");
    editFigure->setFocusPolicy(Qt::NoFocus);
    popupLayout->addWidget(editFigure);
    figurePopup->hide();
    figureTimer.setSingleShot(true); figureTimer.setInterval(550);
    connect(&figureTimer, &QTimer::timeout, this, [this] {
        if (hoveredFigure < 0 || hoveredFigure != figureAt(mapFromGlobal(QCursor::pos()))) return;
        figurePopup->adjustSize();
        figurePopup->move(mapToGlobal(mapFromGlobal(QCursor::pos()) + QPoint(10, 14)));
        figurePopup->show(); figureLeaveTimer.start();
    });
    figureLeaveTimer.setInterval(150);
    connect(&figureLeaveTimer, &QTimer::timeout, this, [this] {
        if (!underMouse() && !figurePopup->underMouse()) hideFigurePopup();
    });
    connect(editFigure, &QPushButton::clicked, this, [this] { activateFigure(hoveredFigure); });
    setAttribute(Qt::WA_AcceptTouchEvents);
    grabGesture(Qt::PinchGesture);
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
    updateLinkCursor(underMouse() ? mapFromGlobal(QCursor::pos()) : QPoint(-1, -1), QApplication::keyboardModifiers());
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
    if (dragging || magnifier->isVisible()) {
        hideFigurePopup();
        setToolTip(QString());
        setCursor(dragging ? Qt::ClosedHandCursor : Qt::BlankCursor);
        return;
    }
    // The saved-PDF viewer identifies links even while a modifier is held.
    // Modified clicks still retain their source-sync and zoom actions.
    const int link = linkAt(point);
    const int figure = link < 0 && modifiers == Qt::NoModifier ? figureAt(point) : -1;
    if (figure != hoveredFigure) {
        hideFigurePopup(); hoveredFigure = figure;
        if (figure >= 0) figureTimer.start();
    }
    setToolTip(link < 0 ? QString() : annotations.value("links").toArray()[link].toObject().value("uri").toString());
    setCursor(link < 0 ? ViewerControls::toolCursor(tool, modifiers) : QCursor(Qt::PointingHandCursor));
}
void TexpressoCanvas::resetTools()
{
    hideFigurePopup();
    ++lensToken;
    if (preview) preview->cancelRegion();
    interactionActive = false; dragging = false; pressedLink = -1; linkDragged = false;
    setToolTip(QString()); magnifier->hide(); magnifier->invalidate(); setTool(tool);
}
void TexpressoCanvas::setTikzFigures(const QJsonArray &figures, bool available)
{
    if (tikzFigures == figures && tikzAvailable == available) return;
    hideFigurePopup(); tikzFigures = figures; tikzAvailable = available;
}
int TexpressoCanvas::figureAt(const QPoint &point) const
{
    if (!tikzAvailable || !fresh || pixels.isNull() || scale <= 0 || !rect().contains(point)) return -1;
    const auto p = pageBounds.topLeft() + QPointF(point) / scale;
    for (int i = 0; i < tikzFigures.size(); ++i) {
        const auto b = tikzFigures[i].toObject()["bounds"].toArray();
        if (b.size() == 4 && QRectF(QPointF(b[0].toDouble(), b[1].toDouble()),
            QPointF(b[2].toDouble(), b[3].toDouble())).contains(p)) return i;
    }
    return -1;
}
void TexpressoCanvas::hideFigurePopup()
{
    figureTimer.stop(); figureLeaveTimer.stop(); hoveredFigure = -1; figurePopup->hide();
}
void TexpressoCanvas::activateFigure(int index)
{
    if (!fresh || !tikzAvailable || index < 0 || index >= tikzFigures.size()) return;
    const int original = tikzFigures[index].toObject()["index"].toInt(-1);
    hideFigurePopup();
    if (original >= 0) emit tikzActivated(annotationPage, original, annotationRevision, annotationSequence);
}
void TexpressoCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = figureAt(event->pos());
    const auto snapshot = tikzFigures;
    const auto revision = annotationRevision, sequence = annotationSequence;
    const int page = annotationPage;
    hideFigurePopup();
    QMenu menu(this);
    QAction *edit = nullptr;
    if (index >= 0) { edit = menu.addAction(tr("Edit in TikZ Editor")); menu.addSeparator(); }
    menu.addActions(actions());
    if (menu.exec(event->globalPos()) == edit && edit && snapshot == tikzFigures &&
        revision == annotationRevision && sequence == annotationSequence && page == annotationPage) activateFigure(index);
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
bool TexpressoCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        const auto gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const qreal factor = 1 + gesture->value();
            if (std::isfinite(factor) && factor > 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                emit scaleRequested(factor, mapFromGlobal(gesture->globalPosition().toPoint()));
#else
                emit scaleRequested(factor, mapFromGlobal(gesture->screenPos().toPoint()));
#endif
            }
            event->accept(); return true;
        }
    } else if (event->type() == QEvent::Gesture) {
        const auto gestures = static_cast<QGestureEvent *>(event);
        if (const auto pinch = static_cast<QPinchGesture *>(gestures->gesture(Qt::PinchGesture))) {
            if (pinch->state() == Qt::GestureStarted) pinchTotal = 1;
            const qreal total = pinch->totalScaleFactor();
            if (std::isfinite(total) && total > 0 && pinchTotal > 0) {
#ifdef Q_OS_MACOS
                // Qt's macOS recognizer stores a widget-local center at the
                // start, but updates the global hotspot as the cursor moves.
                // The canvas itself can move while zooming/scrolling.
                const auto global = pinch->hasHotSpot() ? pinch->hotSpot()
                    : QPointF(mapToGlobal(pinch->centerPoint().toPoint()));
#else
                const auto global = pinch->centerPoint();
#endif
                emit scaleRequested(total / pinchTotal, mapFromGlobal(global.toPoint()));
                pinchTotal = total;
            }
            gestures->accept(pinch); return true;
        }
    }
    const bool handled = QWidget::event(event);
    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::FocusIn:
        updateLinkCursor(mapFromGlobal(QCursor::pos()), QApplication::keyboardModifiers());
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        const auto key = static_cast<QKeyEvent *>(event);
        auto modifiers = key->modifiers();
        // A modifier key's own flag can describe the state before the event.
        // Normalize it so releasing Shift restores the cursor immediately.
        Qt::KeyboardModifier changed = Qt::NoModifier;
        switch (key->key()) {
        case Qt::Key_Shift: changed = Qt::ShiftModifier; break;
        case Qt::Key_Control: changed = Qt::ControlModifier; break;
        case Qt::Key_Meta: changed = Qt::MetaModifier; break;
        case Qt::Key_Alt: changed = Qt::AltModifier; break;
        }
        if (changed != Qt::NoModifier) modifiers.setFlag(changed, event->type() == QEvent::KeyPress);
        updateLinkCursor(mapFromGlobal(QCursor::pos()), modifiers);
        break;
    }
    default:
        break;
    }
    return handled;
}
void TexpressoCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (pixels.isNull()) {
        painter.setPen(Qt::darkGray);
        painter.drawText(rect().adjusted(16, 20, -16, -20), Qt::AlignTop | Qt::AlignHCenter,
            preview && property("texpressoPage").toInt() >= preview->pageCount()
                ? tr("Compiling next page…") : tr("Rendering page…"));
        return;
    }
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
    setToolTip(QString()); setCursor(Qt::BlankCursor);
}
void TexpressoCanvas::mousePressEvent(QMouseEvent *event)
{
    hideFigurePopup();
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
        if (!preview || preview->zoom != -2 || !config->disableHorizontalScrollingForFitToTextWidth)
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
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && linkAt(event->pos()) < 0) {
        interactionActive = true; dragging = false;
        magnify(event->pos());
    }
}
void TexpressoCanvas::leaveEvent(QEvent *event)
{
    if (figurePopup->isVisible()) { figureLeaveTimer.start(); setToolTip(QString()); }
    else updateLinkCursor(QPoint(-1, -1), QApplication::keyboardModifiers());
    QWidget::leaveEvent(event);
}
void TexpressoCanvas::wheelEvent(QWheelEvent *event)
{
    hideFigurePopup();
    if ((event->modifiers() & Qt::ControlModifier) && event->pixelDelta().y()) {
        emit scaleRequested(std::pow(1.002, event->pixelDelta().y()), event->position().toPoint()); event->accept();
    } else if ((event->modifiers() & Qt::ControlModifier) && event->angleDelta().y()) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1, event->position().toPoint()); event->accept();
    } else event->ignore();
}
TexpressoWidget::TexpressoWidget(TexpressoManager *manager, PDFDocumentConfig *config,
    const ViewerControls::Icons &icons, QWidget *parent) : QWidget(parent), manager(manager), config(config), icons(icons)
{
    setObjectName("texpressoLivePreview"); setMinimumWidth(280);
    auto layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    // Mirror the embedded PDF viewer: one icon toolbar above the pages and a
    // status bar with synchronization, messages and zoom below them.
    toolbar = new QToolBar(this); toolbar->setObjectName("texpressoViewerToolbar"); toolbar->setIconSize(QSize(16, 16)); layout->addWidget(toolbar);
    for (const auto &id : {"magnify", "scroll", "firstPage", "previous", "next", "last", "actualSize", "fitToWidth", "fitToTextWidth", "fitToWindow", "zoomIn", "zoomOut", "gotoSource", "followCursor"})
        actions.insert(id, ViewerControls::createAction(id, this, icons));
    ViewerControls::addToolbarActions(toolbar, actions);
    pages = ViewerControls::insertPageControls(toolbar, actions["next"], false);
    auto tools = new QActionGroup(this); tools->addAction(actions["magnify"]); tools->addAction(actions["scroll"]);
    actions[config->editTool == 2 ? "scroll" : "magnify"]->setChecked(true);
    const auto liveAction = [this](const QString &id, const QString &text, const QString &icon, const QString &toolTip) {
        auto action = new QAction(this->icons(icon), text, this); action->setToolTip(toolTip);
        actions.insert(id, action); return action;
    };
    auto build = liveAction("build", tr("Build && Refresh"), "build", tr("Build & Refresh, including the bibliography"));
    auto autoRerun = liveAction("autoRerun", tr("Automatic Reruns"), QString(), tr("Rerun TeX after edits to settle references"));
    autoRerun->setCheckable(true); autoRerun->setChecked(true);
    auto rerunOnce = liveAction("rerunOnce", tr("Rerun Once"), QString(), tr("Run one more TeX pass. Use Build & Refresh to run BibTeX or Biber."));
    auto pause = liveAction("pause", tr("Pause Live Compilation"), "pause", tr("Pause Live Compilation"));
    auto stop = liveAction("stop", tr("Stop Live Preview"), "stop", tr("Stop Live Preview"));
    auto restart = liveAction("restart", tr("Restart Live Preview"), "compile", tr("Restart Live Preview"));
    auto output = liveAction("output", tr("Show Output"), "viewlog", tr("Show Output"));
    auto sync = liveAction("sync", tr("Go to Cursor"), "goto", tr("Show the editor cursor in the preview"));
    auto continuousAction = liveAction("continuous", tr("Continuous Scrolling"), QString(), QString());
    continuousAction->setCheckable(true); continuousAction->setChecked(true);
    // Live compilation controls take the place of the PDF viewer's window buttons.
    auto spacer = new QWidget(toolbar); spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    toolbar->addAction(build); toolbar->addSeparator();
    // Restart replaces Pause/Stop while the engine is stopped or failed.
    for (auto action : {pause, stop, restart}) toolbar->addAction(action);
    toolbar->widgetForAction(pause)->setObjectName("texpressoPause");
    restart->setVisible(false);
    connect(autoRerun, &QAction::toggled, this, [this](bool enabled) { if (shown) shown->setAutoRerun(enabled); });
    connect(rerunOnce, &QAction::triggered, this, [this] { if (shown) shown->rerunOnce(); });
    auto setup = toolbar->addAction(icons("configure"), tr("TeX Environment…"));
    connect(setup, &QAction::triggered, this, &TexpressoWidget::environmentSetupRequested);
    connect(build, &QAction::triggered, this, &TexpressoWidget::buildRequested);
    area = new QScrollArea(this); area->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    area->setWidgetResizable(false); area->setBackgroundRole(QPalette::Dark); area->setFrameStyle(QFrame::NoFrame);
    documentCanvas = new QWidget; documentCanvas->setObjectName("texpressoPages");
    canvas = new TexpressoCanvas(config, area); canvas->setParent(documentCanvas);
    canvas->setObjectName("texpressoActivePage"); area->setWidget(documentCanvas); layout->addWidget(area, 1);
    setFocusProxy(canvas);
    statusBar = new QStatusBar(this); statusBar->setObjectName("texpressoStatusBar"); statusBar->setSizeGripEnabled(false);
    layout->addWidget(statusBar);
    const auto statusButton = [this](QAction *action, const char *name) {
        auto button = new QToolButton(statusBar); button->setDefaultAction(action); button->setObjectName(name);
        statusBar->addPermanentWidget(button);
    };
    statusButton(actions["followCursor"], "texpressoFollow");
    statusButton(sync, "texpressoSync");
    statusIcon = new QLabel(statusBar); statusIcon->hide(); statusBar->addPermanentWidget(statusIcon);
    status = new QLabel(statusBar); status->setObjectName("texpressoStatus");
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); statusBar->addPermanentWidget(status, 1);
    statusButton(output, "texpressoOutput");
    zoomControls = ViewerControls::createZoomControls(statusBar, icons, [this](qreal value) { setZoom(value); }, [this] {
        bool ok; const int value = QInputDialog::getInt(this, tr("Custom Zoom"), tr("Zoom Factor (%):"), 100, 13, 400, 10, &ok);
        if (ok) setZoom(value / 100.0);
    });
    for (QWidget *widget : QList<QWidget *>{zoomControls.scale, zoomControls.out, zoomControls.slider, zoomControls.in}) statusBar->addPermanentWidget(widget);
    connect(continuousAction, &QAction::toggled, this, [this](bool enabled) {
        continuous = enabled; extentPages = 1; layoutPage(); showPage(shown ? shown->preview.page() : 0); updateViewport();
    });
    const auto separator = [this] { auto action = new QAction(this); action->setSeparator(true); return action; };
    contextActions = {actions["magnify"], actions["scroll"], separator(), actions["zoomIn"], actions["zoomOut"],
        separator(), continuousAction, separator(), rerunOnce, autoRerun, separator(), actions["gotoSource"]};
    canvas->hide();
    widthTimer.setSingleShot(true); widthTimer.setInterval(100);
    connect(&widthTimer, &QTimer::timeout, this, [this] {
        if (!previewEnabled) return;
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
        if (previewEnabled && !refreshTimer.isActive()) refreshTimer.start();
    });
    const auto navigate = [this](const QString &id, const std::function<int(TexpressoPreview &)> &target) {
        connect(actions[id], &QAction::triggered, this, [this, target] { if (shown) { shown->preview.navigateTo(target(shown->preview)); showPage(shown->preview.page()); } });
    };
    navigate("firstPage", [](TexpressoPreview &) { return 0; });
    navigate("previous", [](TexpressoPreview &p) { return p.page() - 1; });
    navigate("next", [](TexpressoPreview &p) { return p.page() + 1; });
    connect(actions["last"], &QAction::triggered, this, [this] {
        if (!shown) return;
        const bool known = !shown->preview.canDiscoverPages();
        shown->preview.lastPage();
        if (known) showPage(shown->preview.page());
    });
    connect(pages.number, &QLineEdit::returnPressed, this, [this] { if (shown) { shown->preview.navigateTo(pages.number->text().toInt() - 1); showPage(shown->preview.page()); } });
    connect(actions["actualSize"], &QAction::triggered, this, [this] { setZoom(1); });
    connect(actions["fitToWidth"], &QAction::triggered, this, [this] { setZoom(0); });
    connect(actions["fitToTextWidth"], &QAction::triggered, this, [this] { setZoom(-2); });
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
    connect(actions["followCursor"], &QAction::toggled, this, [this](bool enabled) { updateStateIcons(); emit followCursorChanged(enabled); });
    connect(actions["pause"], &QAction::triggered, this, [this] { if (shown) shown->setPaused(!shown->userPaused); });
    connect(actions["stop"], &QAction::triggered, this, [this] { if (shown) shown->stop(); });
    connect(actions["restart"], &QAction::triggered, manager, &TexpressoManager::restart);
    connect(actions["sync"], &QAction::triggered, this, &TexpressoWidget::syncRequested);
    connect(actions["gotoSource"], &QAction::triggered, this, [this] {
        if (shown && canvas->fresh) shown->preview.inverse(QPointF(canvas->pageBounds.center()));
    });
    connectCanvas(canvas);
    connect(actions["output"], &QAction::triggered, this, &TexpressoWidget::outputRequested);
    const auto saveScroll = [this] {
        if (previewEnabled && shown && !updating) {
            shown->preview.scroll = {area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
            updateViewport(true);
        }
    };
    connect(area->horizontalScrollBar(), &QScrollBar::valueChanged, this, saveScroll);
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, saveScroll);
    refresh();
}
void TexpressoWidget::setPreviewVisible(bool visible)
{
    if (previewEnabled == visible) return;
    previewEnabled = visible;
    widthTimer.stop(); refreshTimer.stop();
    canvas->resetTools();
    for (auto page : extraCanvases) page->resetTools();
    manager->setPreviewVisible(visible);
    if (visible) refresh();
}
void TexpressoWidget::configureAppearance(bool modern, int iconSize)
{
    toolbar->setIconSize(QSize(iconSize, iconSize));
    for (auto button : statusBar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly))
        button->setIconSize(QSize(iconSize, iconSize));
    pages.number->setStyleSheet(modern ? "QLineEdit{ color: white; padding-top: -1px; margin-right: 2px; }"
                                      : "QLineEdit{ padding-top: -1px; margin-right: 2px; }");
}
void TexpressoWidget::setTikzEditor(const QString &executable)
{
    tikzEditor = executable; refresh();
}
void TexpressoWidget::refresh()
{
    refreshTimer.stop();
    if (!previewEnabled) return;
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
        zoomScrollPosition = QPoint(-1, -1); zoomScrollRemainder = {};
        knownBounds.clear(); extentPages = 1; viewportPage = session ? session->preview.page() : 0;
        canvas->pixels = {}; canvas->setProperty("texpressoPage", -1);
        for (auto page : extraCanvases) { page->hide(); page->pixels = {}; page->setProperty("texpressoPage", -1); }
        disconnect(externalLinkConnection);
        if (session) externalLinkConnection = connect(&session->preview, &TexpressoPreview::externalLinkRequested,
            this, [this](const QUrl &url) { ViewerLinks::openUrl(url, this); });
    }
    canvas->setPreview(session ? &session->preview : nullptr);
    if (switched) canvas->resetTools();
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
        const bool discover = preview.canDiscoverPages();
        const auto estimate = preview.estimatedPageCount();
        const bool noPages = !discover && !preview.pageCount() && preview.pageImage(preview.page()).isNull();
        const bool canCompile = !session->userPaused && !session->userStopped && !session->failed() && !session->externalBuildRunning();
        pages.validator->setTop(std::max(1, discover ? std::max(preview.page() + 1, preview.navigablePages()) : preview.pageCount()));
        pages.number->setEnabled(!noPages);
        if (!pages.number->hasFocus() || switched || noPages) pages.number->setText(noPages ? QString() : QString::number(preview.page() + 1));
        pages.count->setForegroundRole(estimate || preview.paginationStopped() ? QPalette::PlaceholderText : QPalette::WindowText);
        pages.count->setText(estimate ? QString::number(*estimate) : preview.complete() || preview.paginationStopped() ? QString::number(preview.pageCount())
            : preview.pageCount() ? tr("%1+").arg(preview.pageCount()) : QString::fromUtf8("…"));
        pages.count->setToolTip(preview.complete() ? tr("Total pages in the completed document.")
            : estimate ? (preview.paginationStopped()
                ? tr("Previous total: %1 pages. Compilation stopped after %2 pages; check Output.").arg(*estimate).arg(preview.pageCount())
                : tr("Previous total: %1 pages. Rechecking after edits; %2 pages are currently available.").arg(*estimate).arg(preview.pageCount()))
            : preview.paginationStopped() ? tr("Compilation stopped after %1 pages. The total is unknown; check Output.").arg(preview.pageCount())
            : tr("%1 pages available so far. Last Page calculates the total and goes to the end.").arg(preview.pageCount()));
        for (const auto &id : {"firstPage", "previous"}) actions[id]->setEnabled(preview.page() > 0);
        actions["next"]->setEnabled((discover && canCompile) || preview.page() + 1 < preview.pageCount());
        actions["last"]->setEnabled(!preview.findingLastPage() && (discover ? canCompile : (preview.pageCount() > 0 && preview.page() + 1 < preview.pageCount())));
        actions["last"]->setToolTip(preview.findingLastPage() ? tr("Finding last page. Navigate or pause to cancel.")
            : discover ? tr("Compile through the end to find the last page and total page count.") : tr("Go to the last page."));
        const bool running = !session->userStopped && !session->failed();
        actions["pause"]->setVisible(running); actions["stop"]->setVisible(running); actions["restart"]->setVisible(!running);
        const QString details = QStringList{session->compilerError, session->compilerWarning, session->previewError}.join('\n').trimmed();
        if (session->failed()) setStatus(session->status, StatusError);
        else if (session->userStopped || session->userPaused || session->externalBuildRunning()) setStatus(session->status);
        else if (!session->compilerError.isEmpty()) setStatus(tr("TeX error: %1").arg(session->compilerError.section('\n', 0, 0)), StatusError, details);
        else if (preview.complete() && !preview.pageCount()) setStatus(tr("No pages to preview."));
        else if (!session->previewError.isEmpty()) setStatus(session->previewError, StatusError, details);
        else if (preview.findingLastPage()) setStatus(tr("Finding last page…"), StatusPlain, session->root());
        else if (!session->compilerWarning.isEmpty()) setStatus(session->compilerWarning, StatusWarning, details);
        // Ordinary edits should not flash between a filename and progress text.
        // Keep this space available for actionable diagnostics and explicit tasks.
        else setStatus(QString());
        updateCanvas(canvas, preview.page());
        shownInvert = config->invertColors; shownGray = config->grayscale;
        actions["gotoSource"]->setEnabled(canvas->fresh);
        if (switched) {
            disconnect(revealConnection);
            revealConnection = connect(&preview, &TexpressoPreview::reveal, this, [this, session](const QPointF &point) {
                if (!previewEnabled || shown != session) return;
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
        setStatus(tr("Open a saved LaTeX project to start live preview."));
        canvas->pixels = {}; canvas->fresh = false; canvas->setAnnotations(-1, -1, -1, {}); canvas->hide();
        for (auto page : extraCanvases) { page->hide(); page->pixels = {}; page->fresh = false; page->setPreview(nullptr); page->setAnnotations(-1, -1, -1, {}); }
        documentCanvas->resize(1, 1); pages.count->clear();
    }
    updateStateIcons();
    updating = false;
    if (shown) updateViewport();
}
void TexpressoWidget::setStatus(const QString &text, Severity severity, const QString &toolTip)
{
    // Rich text lets long compiler context wrap instead of spanning the screen.
    status->setText(text); status->setToolTip(Qt::convertFromPlainText(toolTip.isEmpty() ? text : toolTip));
    if (severity == statusSeverity) return;
    statusSeverity = severity;
    const int size = status->fontMetrics().height();
    if (severity != StatusPlain) statusIcon->setPixmap(icons(severity == StatusError ? "error" : "warning").pixmap(size, size));
    statusIcon->setVisible(severity != StatusPlain);
}
void TexpressoWidget::updateStateIcons()
{
    // Refresh runs for every protocol batch while typing; only reload icons
    // when a toggle changes. The on/off pair follows the PDF viewer's icons.
    const bool follow = actions["followCursor"]->isChecked(), paused = shown && shown->userPaused;
    const int state = int(follow) | int(paused) << 1;
    if (state == iconState) return;
    iconState = state;
    actions["followCursor"]->setIcon(icons(follow ? "syncViewer" : "syncViewer-off"));
    auto pause = actions["pause"];
    pause->setIcon(icons(paused ? "compile" : "pause"));
    pause->setText(paused ? tr("Resume Live Compilation") : tr("Pause Live Compilation"));
    pause->setToolTip(pause->text());
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
    pageCanvas->setTikzFigures(tikzEditor.isEmpty() ? QJsonArray() : shown->tikzFigures(page), !tikzEditor.isEmpty());
    if (!pageCanvas->fresh) pageCanvas->resetTools();
    if (changed) pageCanvas->update();
}
void TexpressoWidget::connectCanvas(TexpressoCanvas *pageCanvas)
{
    connect(pageCanvas, &TexpressoCanvas::tikzActivated, this, [this](int page, int index, qint64 revision, qint64 sequence) {
        if (!shown) return;
        const auto figure = shown->tikzFigure(page, index, revision, sequence);
        if (!figure.isEmpty()) emit tikzRequested(figure["file"].toString(), figure["line"].toInt(), figure["endLine"].toInt());
    });
    connect(pageCanvas, &TexpressoCanvas::linkActivated, this, [this](int page, int index, qint64 revision, qint64 sequence) {
        if (shown) shown->preview.activateLink(page, index, revision, sequence);
    });
    connect(pageCanvas, &TexpressoCanvas::interactionFinished, this, [this] { if (!refreshTimer.isActive()) refreshTimer.start(); });
    pageCanvas->setContextMenuPolicy(Qt::DefaultContextMenu);
    pageCanvas->addActions(contextActions);
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
    connect(pageCanvas, &TexpressoCanvas::scaleRequested, this, [this, pageCanvas](qreal factor, const QPoint &anchor) {
        if (!std::isfinite(factor) || factor <= 0) return;
        setZoom(std::clamp(pageCanvas->scale * 72 / 96 * factor, .125, 4.0), pageCanvas->mapTo(canvas, anchor));
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
        // Navigate to the anchor itself, even when it was already visible.
        // As in the extension, leave a small inset above/left of the target.
        area->horizontalScrollBar()->setValue(qRound(target.x()) - 12);
        area->verticalScrollBar()->setValue(qRound(target.y()) - 12);
    }
    preview.scroll = {area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
    canvas->update(); updating = before;
    if (!updating) updateViewport();
}
void TexpressoWidget::setZoom(qreal value, const QPoint &anchor)
{
    if (!shown) return;
    const QPoint viewportPosition = anchor.x() < 0 ? area->viewport()->rect().center() : canvas->mapTo(area->viewport(), anchor);
    const QPoint scrollPosition(area->horizontalScrollBar()->value(), area->verticalScrollBar()->value());
    if (scrollPosition != zoomScrollPosition) zoomScrollRemainder = {};
    const QPointF documentPoint = QPointF(documentCanvas->mapFrom(area->viewport(), viewportPosition)) + zoomScrollRemainder;
    int page = shown->preview.page();
    for (int i = 0; i < pageRects.size(); ++i) if (QRectF(pageRects[i]).contains(documentPoint)) { page = i; break; }
    const auto oldRect = page < pageRects.size() ? pageRects[page] : QRect();
    // Keep the point in page coordinates, rather than fractions of the rounded
    // widget dimensions. Retain subpixel scroll offsets across small pinch steps.
    const QPointF pagePoint = oldRect.isEmpty() ? QPointF() : (documentPoint - oldRect.topLeft()) / canvas->scale;
    shown->preview.zoom = value; canvas->resetTools();
    if (value == -2) lastZoomMode = 1; // Recenter even when the mode is already selected.
    const bool before = updating; updating = true; layoutPage();
    if (page < pageRects.size()) {
        const auto target = pageRects[page];
        const QPointF wanted = QPointF(target.topLeft()) + pagePoint * canvas->scale - viewportPosition;
        const auto position = [](QScrollBar *bar, qreal wanted) {
            bar->setValue(qRound(wanted));
            return wanted >= bar->minimum() && wanted <= bar->maximum() ? wanted - bar->value() : 0;
        };
        zoomScrollRemainder.setX(value != -2 ? position(area->horizontalScrollBar(), wanted.x()) : 0);
        zoomScrollRemainder.setY(position(area->verticalScrollBar(), wanted.y()));
        zoomScrollPosition = {area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
    }
    updating = before; updateViewport(true);
}
void TexpressoWidget::layoutPage()
{
    if (!previewEnabled) return;
    if (!shown || canvas->pageBounds.isEmpty()) { canvas->hide(); return; }
    auto &preview = shown->preview;
    const bool retainedPage = preview.paginationStopped() && !preview.pageImage(preview.page()).isNull();
    if (!preview.canDiscoverPages() && !preview.pageCount() && !retainedPage) {
        pageRects.clear(); extentPages = 0; canvas->hide();
        for (auto page : extraCanvases) page->hide();
        documentCanvas->resize(1, 1); return;
    }
    if (preview.pageValid(preview.page())) knownBounds.insert(preview.page(), canvas->pageBounds);
    const auto bounds = canvas->pageBounds;
    const auto text = preview.horizontalTextRange();
    const bool fitText = preview.zoom == -2 && !text.isEmpty();
    const auto previousScale = canvas->scale;
    const int previousWidth = documentCanvas->width();
    const double widthScale = std::max(0.05, (area->viewport()->width() - 16.0) / bounds.width());
    canvas->scale = fitText ? std::clamp((area->viewport()->width() - 16.0) / text.width(), .125 * 96 / 72, 4.0 * 96 / 72)
        : preview.zoom == 0 || preview.zoom == -2 ? widthScale : preview.zoom == -1
        ? std::min(widthScale, std::max(0.05, (area->viewport()->height() - 16.0) / bounds.height())) : preview.zoom * 96.0 / 72;
    // Only produced pages and one discovery placeholder can grow the layout.
    // An arbitrary requested page must never create thousands of white sheets.
    const int layoutCount = std::max(preview.pageCount() + (preview.canDiscoverPages() ? 1 : 0), retainedPage ? preview.page() + 1 : 0);
    const int count = continuous ? std::clamp(layoutCount, 1, 10000) : 1;
    extentPages = preview.canDiscoverPages() ? std::max(extentPages, count) : count;
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
    if (fitText && (previousScale != canvas->scale || previousWidth != width || preview.zoom != lastZoomMode
        || lastTextRange != text || config->disableHorizontalScrollingForFitToTextWidth)) {
        const bool before = updating; updating = true;
        area->horizontalScrollBar()->setValue(qRound(canvas->x() + text.left() * canvas->scale) - 8);
        updating = before;
    }
    lastZoomMode = preview.zoom; lastTextRange = text;
    const qreal factor = canvas->scale * 72 / 96;
    zoomControls.scale->setText(QString::number(qRound(factor * 100)) + '%');
    { const QSignalBlocker block(zoomControls.slider); zoomControls.slider->setValue(ViewerControls::scaleToSlider(factor)); }
    actions["fitToWidth"]->setChecked(preview.zoom == 0); actions["fitToWindow"]->setChecked(preview.zoom == -1);
    actions["fitToTextWidth"]->setChecked(preview.zoom == -2);
    actions["zoomIn"]->setEnabled(factor < 4); zoomControls.in->setEnabled(factor < 4);
    actions["zoomOut"]->setEnabled(factor > .125); zoomControls.out->setEnabled(factor > .125);
    const int widthPixels = qRound(canvas->width() * devicePixelRatioF());
    if (shown != rasterSession || widthPixels != rasterWidth) {
        rasterSession = shown; rasterWidth = widthPixels; widthTimer.start();
    }
}
void TexpressoWidget::updateViewport(bool gesture)
{
    if (!previewEnabled || !shown || updating || pageRects.isEmpty()) return;
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
