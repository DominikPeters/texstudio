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
    void explicitLinkPositionsAnchor_data();
    void explicitLinkPositionsAnchor();
    void scrollingHandoffRetainsVisiblePixels();
    void scrollFollowingUsesPublishedPageWithoutFocus();
    void fitTextWidth();
    void fitTextWidthBlankAndEditedPages();
private:
    void frame(int page = 0, QRectF bounds = QRectF(0, 0, 2000, 1200), QRectF text = {});
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
void SourceViewportTest::frame(int page, QRectF bounds, QRectF text)
{
    auto &preview = session->preview;
    QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setPage(page);
    if (preview.revision() < 0) preview.message({"preview-state", 1, double(preview.sequence()), 3, true, 0});
    if (!preview.valid()) {
        QVERIFY(!commands.isEmpty());
        const auto request = commands.last()[0].toJsonArray();
        QCOMPARE(request[0].toString(), QString("preview-render"));
        QImage image(16, 24, QImage::Format_RGB32); image.fill(page == 0 ? Qt::red : Qt::blue);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        QJsonObject header{{"requestId", request[1]}, {"revision", request[4]}, {"sequence", request[5]},
            {"page", page}, {"width", image.width()}, {"height", image.height()},
            {"bounds", QJsonArray{bounds.left(), bounds.top(), bounds.right(), bounds.bottom()}},
            {"mime", "image/png"}, {"byteLength", png.size()}};
        if (!text.isEmpty()) header["textBounds"] = QJsonArray{text.left(), text.top(), text.right(), text.bottom()};
        preview.receive(header, png);
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
void SourceViewportTest::explicitLinkPositionsAnchor_data()
{
    QTest::addColumn<bool>("continuous");
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<QPointF>("anchor");
    for (bool continuous : {false, true}) {
        const auto mode = continuous ? "continuous" : "single";
        QTest::newRow(qPrintable(QString("%1-visible").arg(mode))) << continuous << qreal(.75) << QPointF(400, 250);
        QTest::newRow(qPrintable(QString("%1-below-fold-zoomed").arg(mode))) << continuous << qreal(1.25) << QPointF(500, 1000);
        QTest::newRow(qPrintable(QString("%1-page-origin").arg(mode))) << continuous << qreal(.75) << QPointF(20, 30);
        QTest::newRow(qPrintable(QString("%1-page-bottom").arg(mode))) << continuous << qreal(.75) << QPointF(1820, 1530);
    }
}
void SourceViewportTest::explicitLinkPositionsAnchor()
{
    QFETCH(bool, continuous); QFETCH(qreal, zoom); QFETCH(QPointF, anchor);
    widget->continuous = continuous; session->preview.zoom = zoom;
    frame(1, {20, 30, 1800, 1500});
    setScroll({150, widget->pageRects[1].top()});
    const QPointF documentPoint = QPointF(widget->canvas->pos())
        + (anchor - widget->canvas->pageBounds.topLeft()) * widget->canvas->scale;
    widget->revealPoint(anchor, false);
    const QPoint expected(
        std::clamp(qRound(documentPoint.x()) - 12, 0, widget->area->horizontalScrollBar()->maximum()),
        std::clamp(qRound(documentPoint.y()) - 12, 0, widget->area->verticalScrollBar()->maximum()));
    QCOMPARE(scroll(), expected);
    // Repeated/cached same-page links restore the same exact position.
    setScroll({0, 0}); widget->revealPoint(anchor, false); QCOMPARE(scroll(), expected);
    QCOMPARE(session->preview.scroll, expected);
}
void SourceViewportTest::scrollingHandoffRetainsVisiblePixels()
{
    frame(1); frame(0);
    const auto boundary = widget->pageRects[1].top();
    const auto viewport = widget->area->viewport()->height();
    setScroll({250, boundary - viewport / 3});
    widget->updateViewport(true); widget->refreshTimer.stop();
    QCOMPARE(session->preview.page(), 1);
    QCOMPARE(widget->canvas->property("texpressoPage").toInt(), 1);
    QCOMPARE(widget->canvas->geometry(), widget->pageRects[1]);
    QCOMPARE(widget->canvas->pixels.pixelColor(0, 0), QColor(Qt::blue));
    const auto visiblePage = [&](int page) -> TexpressoCanvas * {
        for (auto item : widget->extraCanvases)
            if (item->isVisible() && item->property("texpressoPage").toInt() == page) return item;
        return nullptr;
    };
    auto previous = visiblePage(0); QVERIFY(previous);
    QCOMPARE(previous->pixels.pixelColor(0, 0), QColor(Qt::red));
    // A rerun discards validated cache entries, while both pages are still on
    // screen. Keep their own last pixels with links and source clicks disabled.
    session->preview.message({"preview-invalidated", 2, 0});
    widget->updateViewport(); widget->refreshTimer.stop();
    previous = visiblePage(0); QVERIFY(previous);
    QCOMPARE(previous->pixels.pixelColor(0, 0), QColor(Qt::red)); QVERIFY(!previous->fresh);
    QVERIFY(!widget->canvas->fresh);
    QCOMPARE(previous->grab().toImage().pixelColor(0, 0), QColor(Qt::red));
    // Reverse direction before the replacement frame arrives. The stale red
    // page must transfer from its neighbor canvas to the active one atomically.
    setScroll({250, boundary - viewport * 2 / 3});
    widget->updateViewport(true); widget->refreshTimer.stop();
    QCOMPARE(session->preview.page(), 0);
    QCOMPARE(widget->canvas->property("texpressoPage").toInt(), 0);
    QCOMPARE(widget->canvas->geometry(), widget->pageRects[0]);
    QCOMPARE(widget->canvas->pixels.pixelColor(0, 0), QColor(Qt::red));
    const auto next = visiblePage(1); QVERIFY(next);
    QCOMPARE(next->pixels.pixelColor(0, 0), QColor(Qt::blue)); QVERIFY(!next->fresh);
}
void SourceViewportTest::fitTextWidth()
{
    auto &preview = session->preview;
    preview.sourceChanged(1);
    // Non-zero page origins must not shift the fitted text.
    frame(0, {20, 30, 2000, 1200}, {320, 130, 1400, 900});
    const auto actions = widget->viewerActions();
    QVERIFY(actions.contains("fitToTextWidth"));
    actions["fitToTextWidth"]->setEnabled(true); actions["fitToWindow"]->setEnabled(true);
    QCOMPARE(actions["fitToTextWidth"]->shortcuts(), ViewerControls::actionSpec("fitToTextWidth").shortcuts);
    setScroll({0, 100});
    actions["fitToTextWidth"]->trigger(); widget->widthTimer.stop();
    QCOMPARE(preview.zoom, -2.0);
    QVERIFY(actions["fitToTextWidth"]->isChecked());
    QVERIFY(!actions["fitToWidth"]->isChecked()); QVERIFY(!actions["fitToWindow"]->isChecked());
    const auto check = [this] {
        const auto text = session->preview.horizontalTextRange();
        const auto left = widget->canvas->mapTo(widget->area->viewport(), QPoint()).x() + text.left() * widget->canvas->scale;
        QVERIFY(std::abs(left - 8) <= 1);
        QVERIFY(std::abs(text.width() * widget->canvas->scale - (widget->area->viewport()->width() - 16)) <= 1);
    };
    check();
    widget->resize(800, 600); QCoreApplication::processEvents(); widget->widthTimer.stop(); check();
    // Clicking the selected action recenters after a manual horizontal pan.
    setScroll({0, scroll().y()}); actions["fitToTextWidth"]->trigger(); widget->widthTimer.stop(); check();
    actions["zoomIn"]->trigger(); QVERIFY(preview.zoom > 0); QVERIFY(!actions["fitToTextWidth"]->isChecked());
    actions["fitToWindow"]->trigger(); QCOMPARE(preview.zoom, -1.0); QVERIFY(!actions["fitToTextWidth"]->isChecked());
}
void SourceViewportTest::fitTextWidthBlankAndEditedPages()
{
    auto &preview = session->preview;
    preview.sourceChanged(1); frame(0, {0, 0, 2000, 1200}, {300, 100, 1400, 900});
    widget->viewerActions()["fitToTextWidth"]->setEnabled(true);
    widget->viewerActions()["fitToTextWidth"]->trigger(); widget->widthTimer.stop();
    const auto scale = widget->canvas->scale;
    frame(1); // A blank page must not zoom in on its page number or lose the range.
    QCOMPARE(preview.horizontalTextRange(), QRectF(300, 0, 1400, 1));
    QCOMPARE(widget->canvas->scale, scale);
    preview.releaseCache(); QCOMPARE(preview.horizontalTextRange(), QRectF(300, 0, 1400, 1));
    preview.sourceChanged(2); // Preserve the best geometry while the new pass catches up.
    QCOMPARE(preview.horizontalTextRange(), QRectF(300, 0, 1400, 1));
    frame(0, {0, 0, 2000, 1200}, {250, 100, 1500, 900});
    QCOMPARE(preview.horizontalTextRange(), QRectF(250, 0, 1500, 1));
    preview.sourceChanged(3); frame(0); // The text was deleted.
    QVERIFY(preview.horizontalTextRange().isEmpty());
    QVERIFY(std::isfinite(widget->canvas->scale));
    QVERIFY(widget->canvas->scale < scale); // Falls back to fitting the paper.
}
void SourceViewportTest::scrollFollowingUsesPublishedPageWithoutFocus()
{
    auto &preview = session->preview;
    preview.setCapabilities({{"synctexPrecision", 1}, {"requestLifecycle", true}});
    session->setActive(true); config.followFromScroll = true;
    QSignalSpy commands(&preview, &TexpressoPreview::command), sources(session.get(), &TexpressoSession::sourceLocation);
    widget->updateViewport();
    const auto before = scroll();
    widget->syncVisibleSource();
    QCOMPARE(commands.size(), 1);
    const auto inverse = commands[0][0].toJsonArray();
    QCOMPARE(inverse[0].toString(), QString("preview-inverse-precise"));
    QCOMPARE(inverse[8].toBool(), false);
    const QJsonObject hit{{"file", session->root()}, {"line", 4}, {"column", -1}, {"word", "Source"}};
    preview.message({"preview-inverse-result", inverse[1], inverse[2], inverse[3], inverse[4], hit});
    QCOMPARE(sources.size(), 1); QVERIFY(!sources.last()[4].toBool()); QCOMPARE(scroll(), before);
    config.followFromScroll = false; widget->syncVisibleSource(); QCOMPARE(commands.size(), 1);
    config.followFromScroll = true; session->setPaused(true); commands.clear();
    widget->syncVisibleSource(); QVERIFY(commands.isEmpty());
    session->setPaused(false); preview.sourceChanged(1); commands.clear();
    widget->syncVisibleSource(); QVERIFY(commands.isEmpty()); // Stale pixels cannot drive source movement.
}
QTEST_MAIN(SourceViewportTest)
#include "source_viewport_test.moc"
