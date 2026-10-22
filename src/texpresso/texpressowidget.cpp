#include "texpressowidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextEdit>
#include <QDialog>
#include <cmath>

void TexpressoCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!pixels.isNull()) painter.drawImage(rect(), pixels);
}
void TexpressoCanvas::mousePressEvent(QMouseEvent *event)
{
    if (fresh && (event->modifiers() & Qt::ControlModifier)) {
        const auto point = event->pos();
        emit sourceClicked(QPointF(point.x() / scale + pageBounds.left(), point.y() / scale + pageBounds.top()));
    }
    QWidget::mousePressEvent(event);
}
TexpressoWidget::TexpressoWidget(TexpressoManager *manager, QWidget *parent) : QWidget(parent), manager(manager)
{
    setObjectName("texpressoLivePreview"); setMinimumWidth(280);
    auto layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4);
    project = new QLabel(this); project->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(project);
    auto toolbar = new QHBoxLayout;
    const auto button = [this, toolbar](const QString &text, const QString &tip) {
        auto result = new QToolButton(this); result->setText(text); result->setToolTip(tip); toolbar->addWidget(result); return result;
    };
    previous = button(QString::fromUtf8("‹"), tr("Previous page"));
    page = new QSpinBox(this); page->setRange(1, 100001); toolbar->addWidget(page);
    total = new QLabel(this); toolbar->addWidget(total);
    next = button(QString::fromUtf8("›"), tr("Next page"));
    zoom = new QComboBox(this);
    zoom->addItem(tr("Fit width"), 0.0); zoom->addItem(tr("Fit page"), -1.0);
    for (double value : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0}) zoom->addItem(QString::number(value * 100) + '%', value);
    toolbar->addWidget(zoom); toolbar->addStretch(); layout->addLayout(toolbar);
    auto controls = new QHBoxLayout;
    pause = new QToolButton(this); pause->setText(tr("Pause")); controls->addWidget(pause);
    stop = new QToolButton(this); stop->setText(tr("Stop")); controls->addWidget(stop);
    restart = new QToolButton(this); restart->setText(tr("Restart")); controls->addWidget(restart);
    auto output = new QToolButton(this); output->setText(tr("Output")); controls->addWidget(output);
    auto sync = new QToolButton(this); sync->setText(tr("Sync")); sync->setToolTip(tr("Show the editor cursor in the preview")); controls->addWidget(sync);
    controls->addStretch(); layout->addLayout(controls);
    status = new QLabel(this); status->setWordWrap(true); layout->addWidget(status);
    area = new QScrollArea(this); area->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    area->setWidgetResizable(false); canvas = new TexpressoCanvas; area->setWidget(canvas); layout->addWidget(area, 1);
    canvas->hide();
    widthTimer.setSingleShot(true); widthTimer.setInterval(100);
    connect(&widthTimer, &QTimer::timeout, this, [this] {
        if (auto session = this->manager->current()) session->preview.setWidth(qRound(canvas->width() * devicePixelRatioF()));
    });
    connect(manager, &TexpressoManager::updated, this, &TexpressoWidget::refresh);
    connect(previous, &QToolButton::clicked, this, [this] { if (shown) shown->preview.setPage(shown->preview.page() - 1); });
    connect(next, &QToolButton::clicked, this, [this] { if (shown) shown->preview.setPage(shown->preview.page() + 1); });
    connect(page, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { if (shown && !updating) shown->preview.setPage(value - 1); });
    connect(zoom, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (shown && !updating) { shown->preview.zoom = zoom->itemData(index).toDouble(); layoutPage(); }
    });
    connect(pause, &QToolButton::clicked, this, [this] { if (shown) shown->setPaused(!shown->userPaused); });
    connect(stop, &QToolButton::clicked, this, [this] { if (shown) shown->stop(); });
    connect(restart, &QToolButton::clicked, manager, &TexpressoManager::restart);
    connect(sync, &QToolButton::clicked, this, [this] { emit syncRequested(); });
    connect(canvas, &TexpressoCanvas::sourceClicked, this, [this](const QPointF &point) { if (shown) shown->preview.inverse(point); });
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
void TexpressoWidget::refresh()
{
    updating = true;
    auto session = manager->current();
    const bool switched = session != shown;
    shown = session;
    const QFileInfo root(session ? session->root() : QString());
    project->setText(session ? tr("Live — %1 / %2").arg(root.dir().dirName(), root.fileName()) : tr("Live preview"));
    project->setToolTip(session ? session->root() : QString());
    for (auto button : {pause, stop, restart, previous, next}) button->setEnabled(session != nullptr);
    page->setEnabled(session); zoom->setEnabled(session);
    if (session) {
        auto &preview = session->preview;
        page->setMaximum(preview.complete() ? std::max(1, preview.pageCount()) : 100001);
        page->setValue(preview.page() + 1);
        total->setText(preview.complete() ? tr("/ %1").arg(preview.pageCount()) : QString::fromUtf8("/ …"));
        previous->setEnabled(preview.page() > 0);
        next->setEnabled(!preview.complete() || preview.page() + 1 < preview.pageCount());
        pause->setText(session->userPaused ? tr("Resume") : tr("Pause"));
        pause->setEnabled(!session->userStopped && !session->failed());
        stop->setEnabled(!session->userStopped && !session->failed());
        zoom->setCurrentIndex(zoom->findData(preview.zoom));
#ifdef Q_OS_MACOS
        const QString liveHint = tr("Live preview · Command-click to open source");
#else
        const QString liveHint = tr("Live preview · Ctrl-click to open source");
#endif
        status->setText(session->failed() || session->userStopped || session->userPaused ? session->status
            : !session->previewError.isEmpty() ? session->previewError
            : preview.valid() ? liveHint : tr("Updating preview…"));
        canvas->pixels = preview.image(); canvas->pageBounds = preview.bounds(); canvas->fresh = preview.valid() && !session->userStopped && !session->failed();
        if (switched) {
            disconnect(revealConnection);
            revealConnection = connect(&preview, &TexpressoPreview::reveal, this, [this, session](const QPointF &point) {
                if (shown != session) return;
                area->ensureVisible(qRound((point.x() - canvas->pageBounds.left()) * canvas->scale), qRound((point.y() - canvas->pageBounds.top()) * canvas->scale), 40, 60);
            });
        }
        layoutPage();
        if (switched) {
            area->horizontalScrollBar()->setValue(preview.scroll.x());
            area->verticalScrollBar()->setValue(preview.scroll.y());
        }
    } else {
        status->setText(tr("Open a saved LaTeX project to start live preview."));
        canvas->pixels = {}; canvas->fresh = false; canvas->hide(); total->clear();
    }
    updating = false;
}
void TexpressoWidget::layoutPage()
{
    if (!shown || canvas->pixels.isNull() || canvas->pageBounds.isEmpty()) { canvas->hide(); return; }
    const auto bounds = canvas->pageBounds;
    const double widthScale = std::max(0.05, (area->viewport()->width() - 4.0) / bounds.width());
    canvas->scale = shown->preview.zoom == 0 ? widthScale : shown->preview.zoom < 0
        ? std::min(widthScale, std::max(0.05, (area->viewport()->height() - 4.0) / bounds.height())) : shown->preview.zoom * 96.0 / 72;
    canvas->resize(qRound(bounds.width() * canvas->scale), qRound(bounds.height() * canvas->scale));
    canvas->show(); canvas->update(); widthTimer.start();
}
void TexpressoWidget::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); layoutPage(); }
