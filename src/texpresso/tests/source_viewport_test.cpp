#include <QtTest>
#include "texpressowidget.h"
#include <QBuffer>
#include <QScrollBar>
#include <algorithm>
#include <memory>

class SourceViewportTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void visibleTargetsStayStill();
    void eachAxisCentersOnlyWhenNecessary();
    void marginHysteresisAndClamping();
    void secondVisiblePageDoesNotJumpToItsTop();
    void currentFrameGeometryAndStaleTargets();
    void explicitLinkKeepsDestinationNavigation();
private:
    void frame(int page = 0, QRectF bounds = QRectF(0, 0, 2000, 1200));
    QPoint scroll() const;
    void setScroll(const QPoint &point);
    QPointF pagePoint(const QPointF &documentPoint) const;
    PDFDocumentConfig config{};
    std::unique_ptr<TexpressoManager> manager;
    std::unique_ptr<TexpressoSession> session;
    std::unique_ptr<TexpressoWidget> widget;
};
void SourceViewportTest::init()
{
    config = {}; config.editTool = 2;
    manager = std::make_unique<TexpressoManager>();
    widget = std::make_unique<TexpressoWidget>(manager.get(), &config, [](const QString &) { return QIcon(); });
    widget->resize(640, 700); widget->show(); QCoreApplication::processEvents();
    // No process is started: use real frame decoding and controller state with
    // deterministic geometry, independent of installed TeX or screen timing.
    session = std::make_unique<TexpressoSession>("/tmp/source-viewport.tex", QString(), TexpressoSession::Buffers{});
    session->preview.zoom = .75; session->preview.setActive(true);
    widget->shown = session.get();
    frame();
}
void SourceViewportTest::cleanup() { widget.reset(); session.reset(); manager.reset(); }
void SourceViewportTest::frame(int page, QRectF bounds)
{
    auto &preview = session->preview;
    QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setPage(page);
    if (preview.revision() < 0) preview.message({"preview-state", 1, 0, 3, true, 0});
    if (!preview.valid()) {
        QVERIFY(!commands.isEmpty());
        const auto request = commands.last()[0].toJsonArray();
        QCOMPARE(request[0].toString(), QString("preview-render"));
        QImage image(16, 24, QImage::Format_RGB32); image.fill(Qt::white);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        preview.receive({{"requestId", request[1]}, {"revision", request[4]}, {"sequence", request[5]},
            {"page", page}, {"width", image.width()}, {"height", image.height()},
            {"bounds", QJsonArray{bounds.left(), bounds.top(), bounds.right(), bounds.bottom()}},
            {"mime", "image/png"}, {"byteLength", png.size()}}, png);
        QTRY_VERIFY(preview.valid());
    }
    widget->canvas->pixels = preview.image(); widget->canvas->pageBounds = preview.bounds(); widget->canvas->fresh = true;
    widget->updating = true; widget->layoutPage(); widget->updating = false;
    widget->widthTimer.stop();
}
QPoint SourceViewportTest::scroll() const
{
    return {widget->area->horizontalScrollBar()->value(), widget->area->verticalScrollBar()->value()};
}
void SourceViewportTest::setScroll(const QPoint &point)
{
    widget->updating = true;
    widget->area->horizontalScrollBar()->setValue(point.x()); widget->area->verticalScrollBar()->setValue(point.y());
    widget->updating = false;
}
QPointF SourceViewportTest::pagePoint(const QPointF &documentPoint) const
{
    return widget->canvas->pageBounds.topLeft() + (documentPoint - QPointF(widget->canvas->pos())) / widget->canvas->scale;
}
void SourceViewportTest::visibleTargetsStayStill()
{
    setScroll({250, 300}); const auto before = scroll();
    const auto viewport = widget->area->viewport()->size();
    const auto target = QPointF(before) + QPointF(viewport.width() / 2.0, viewport.height() / 2.0);
    widget->revealPoint(pagePoint(target), true); QCOMPARE(scroll(), before);
    // Repeated source positions and small line-to-line movement do not creep.
    for (int offset : {1, -2, 3, 0}) widget->revealPoint(pagePoint(target + QPointF(0, offset)), true);
    QCOMPARE(scroll(), before);
}
void SourceViewportTest::eachAxisCentersOnlyWhenNecessary()
{
    setScroll({250, 300}); const auto before = scroll();
    const auto viewport = widget->area->viewport()->size();
    widget->revealPoint(pagePoint({before.x() + viewport.width() / 2.0, 1100}), true);
    QCOMPARE(scroll().x(), before.x());
    QCOMPARE(scroll().y(), std::clamp(qRound(1100 - viewport.height() / 2.0), 0, widget->area->verticalScrollBar()->maximum()));
    const auto vertical = scroll().y();
    widget->revealPoint(pagePoint({1800, vertical + viewport.height() / 2.0}), true);
    QCOMPARE(scroll().y(), vertical);
    QCOMPARE(scroll().x(), std::clamp(qRound(1800 - viewport.width() / 2.0), 0, widget->area->horizontalScrollBar()->maximum()));
}
void SourceViewportTest::marginHysteresisAndClamping()
{
    setScroll({250, 300}); const auto before = scroll();
    const auto viewport = widget->area->viewport()->size();
    const qreal margin = std::min(48.0, viewport.height() * .1);
    const qreal x = before.x() + viewport.width() / 2.0;
    widget->revealPoint(pagePoint({x, before.y() + margin - 3}), true); QCOMPARE(scroll(), before);
    widget->revealPoint(pagePoint({x, before.y() + margin - 5}), true); QVERIFY(scroll().y() < before.y());
    setScroll({250, 3});
    widget->revealPoint(pagePoint({x, 0}), true); QCOMPARE(scroll().y(), 3); // Clamped correction within four pixels.
    setScroll({250, 100});
    widget->revealPoint(pagePoint({x, 0}), true); QCOMPARE(scroll().y(), 0);
    widget->revealPoint(pagePoint({x, 100000}), true);
    QCOMPARE(scroll().y(), widget->area->verticalScrollBar()->maximum());
}
void SourceViewportTest::secondVisiblePageDoesNotJumpToItsTop()
{
    frame(1);
    const auto page = widget->pageRects[1]; const auto viewport = widget->area->viewport()->size();
    // The preceding page still owns the largest visible area.
    const int top = page.top() - viewport.height() * 2 / 3;
    setScroll({250, top}); const auto before = scroll();
    widget->revealPoint(pagePoint({before.x() + viewport.width() / 2.0, qreal(page.top() + 45)}), true);
    QCOMPARE(scroll(), before);
}
void SourceViewportTest::currentFrameGeometryAndStaleTargets()
{
    frame(1, {20, 30, 1800, 1500});
    setScroll({250, widget->pageRects[1].top() + 300}); const auto before = scroll();
    const auto viewport = widget->area->viewport()->size();
    const auto target = pagePoint(QPointF(before) + QPointF(viewport.width() / 2.0, viewport.height() / 2.0));
    widget->canvas->pageBounds = QRectF(100, 200, 50, 60);
    widget->revealPoint(target, true);
    QCOMPARE(widget->canvas->pageBounds, session->preview.bounds()); QCOMPARE(scroll(), before);
    session->preview.sourceChanged(1);
    widget->revealPoint({100000, 100000}, true); QCOMPARE(scroll(), before);
}
void SourceViewportTest::explicitLinkKeepsDestinationNavigation()
{
    frame(1); setScroll({250, widget->pageRects[1].top() + 300}); const auto before = scroll();
    widget->revealPoint({400, 450}, false);
    QVERIFY(scroll().y() != before.y());
    const auto target = widget->canvas->mapTo(widget->area->viewport(), QPoint(400, 450));
    QVERIFY(widget->area->viewport()->rect().contains(target));
}
QTEST_MAIN(SourceViewportTest)
#include "source_viewport_test.moc"
