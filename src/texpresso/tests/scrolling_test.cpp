#include <QtTest>
#include "texpressopreview.h"
#include "texpressowidget.h"
#include <QBuffer>
#include <QFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QFileInfo>
#include <cmath>

class ScrollingTest : public QObject
{
    Q_OBJECT
private slots:
    void backgroundPriorityAndFences();
    void frontierDoesNotRunAway();
    void visibleExportsSurviveScrollTicks();
    void retainedPixelsKeepTheirPageIdentity();
    void engineOnlyRerunRefreshesVisiblePages();
    void realContinuousViewer();
};
static QByteArray png(Qt::GlobalColor color)
{
    QImage image(16, 24, QImage::Format_RGB32); image.fill(color);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
static QJsonArray last(const QSignalSpy &spy) { return spy.last()[0].toJsonArray(); }
static void deliver(TexpressoPreview &preview, const QJsonArray &request, Qt::GlobalColor color)
{
    const auto bytes = png(color);
    preview.receive({{"requestId", request[1]}, {"revision", request[4]}, {"sequence", request[5]},
        {"page", request[2]}, {"width", 16}, {"height", 24}, {"bounds", QJsonArray{0, 0, 160, 240}},
        {"mime", "image/png"}, {"byteLength", bytes.size()}}, bytes);
}
void ScrollingTest::backgroundPriorityAndFences()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}, {"regions", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 10, true, 0});
    deliver(preview, last(commands), Qt::red); QTRY_VERIFY(preview.valid());
    const auto foreground = preview.image();
    preview.setViewportPages({0, 1});
    QCOMPARE(last(commands)[0].toString(), QString("preview-background"));
    QCOMPARE(last(commands)[2].toInt(), 1); QVERIFY(preview.busy());
    deliver(preview, last(commands), Qt::blue); QTRY_VERIFY(preview.pageValid(1));
    QCOMPARE(preview.image(), foreground); QCOMPARE(preview.page(), 0);
    // Neighbor work must yield to an interactive lens, and its late frame must
    // never replace either the lens or the foreground page.
    preview.setViewportPages({0, 2}); QTRY_COMPARE(last(commands)[0].toString(), QString("preview-background"));
    const auto old = last(commands);
    preview.requestRegion({10, 10, 30, 30}, 60, 1);
    QCOMPARE(last(commands)[0].toString(), QString("preview-region"));
    deliver(preview, old, Qt::green); QTest::qWait(20); QCOMPARE(preview.image(), foreground);
    deliver(preview, last(commands), Qt::blue);
    QTRY_COMPARE(last(commands)[0].toString(), QString("preview-background"));
    QCOMPARE(last(commands)[2].toInt(), 2); // interrupted visible work is retried
    deliver(preview, last(commands), Qt::green); QTRY_VERIFY(preview.pageValid(2));
    preview.sourceChanged(1); deliver(preview, old, Qt::green); QTest::qWait(20); QVERIFY(!preview.pageValid(old[2].toInt()));
    QVERIFY(!preview.valid());
}
void ScrollingTest::visibleExportsSurviveScrollTicks()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 3, true, 0});
    deliver(preview, last(commands), Qt::red); QTRY_VERIFY(preview.valid());
    preview.setViewportPages({0, 1}, true);
    const auto request = last(commands);
    QCOMPARE(request[0].toString(), QString("preview-background"));
    const int sent = commands.size();
    // Trackpad ticks arrive faster than an export. A page that remains visible
    // must be allowed to finish instead of being cancelled on every tick.
    for (int tick = 0; tick < 20; ++tick) preview.setViewportPages({0, 1}, true);
    QCOMPARE(commands.size(), sent);
    deliver(preview, request, Qt::blue); QTRY_VERIFY(preview.pageValid(1));
    QCOMPARE(preview.pageImage(1).pixelColor(0, 0), QColor(Qt::blue));
    preview.setViewportPages({0, 2}, true);
    QCOMPARE(last(commands)[0].toString(), QString("preview-background"));
    const auto obsolete = last(commands);
    preview.setViewportPages({0}, true);
    QCOMPARE(last(commands)[0].toString(), QString("preview-cancel"));
    QCOMPARE(last(commands)[1], obsolete[1]);
}
void ScrollingTest::retainedPixelsKeepTheirPageIdentity()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 3, true, 0});
    deliver(preview, last(commands), Qt::red); QTRY_VERIFY(preview.valid());
    preview.setPage(1);
    QVERIFY(!preview.valid());
    QVERIFY(preview.pageImage(1).isNull());
    QCOMPARE(preview.pageImage(0).pixelColor(0, 0), QColor(Qt::red));
    deliver(preview, last(commands), Qt::blue); QTRY_VERIFY(preview.valid());
    QCOMPARE(preview.pageImage(1).pixelColor(0, 0), QColor(Qt::blue));
    preview.sourceChanged(1);
    QCOMPARE(preview.pageImage(1).pixelColor(0, 0), QColor(Qt::blue));
    QVERIFY(!preview.pageValid(1));
    preview.setPage(2);
    QVERIFY(preview.pageImage(2).isNull());
    QCOMPARE(preview.pageImage(1).pixelColor(0, 0), QColor(Qt::blue));
}
void ScrollingTest::frontierDoesNotRunAway()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, false, 0});
    deliver(preview, last(commands), Qt::red); QTRY_VERIFY(preview.valid());
    preview.setViewportPages({0});
    QTRY_COMPARE(last(commands)[0].toString(), QString("preview-background"));
    QCOMPARE(last(commands)[2].toInt(), 1);
    deliver(preview, last(commands), Qt::blue); QTRY_VERIFY(preview.pageValid(1));
    preview.message({"preview-state", 1, 0, 2, false, 0});
    const int count = commands.size(); QTest::qWait(400); QCOMPARE(commands.size(), count);
    // A cursor preparation blocks idle neighbors, but visible page demand is
    // considered busy so the owner can cancel preparation before rendering.
    preview.setPreparationPending(true); preview.setViewportPages({1}, true);
    QTest::qWait(200); QCOMPARE(commands.size(), count);
    preview.setPreparationPending(false);
    QTRY_COMPARE(last(commands)[2].toInt(), 2);
    QCOMPARE(last(commands)[0].toString(), QString("preview-background"));
}
void ScrollingTest::engineOnlyRerunRefreshesVisiblePages()
{
    // Automatic auxiliary reruns keep the editor sequence unchanged. Both
    // invalidation routes must forget old attempted secondary exports.
    for (bool invalidated : {false, true}) {
        TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
        preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
        preview.message({"preview-state", 1, 0, 2, true, 0});
        deliver(preview, last(commands), Qt::red); QTRY_VERIFY(preview.valid());
        preview.setViewportPages({0, 1});
        const auto oldSecondary = last(commands);
        QCOMPARE(oldSecondary[0].toString(), QString("preview-background"));
        deliver(preview, oldSecondary, Qt::blue); QTRY_VERIFY(preview.pageValid(1));
        if (invalidated) preview.message({"preview-invalidated", 2, 0});
        else preview.message({"preview-state", 2, 0, 2, true, 0});
        QCOMPARE(preview.sequence(), 0); QCOMPARE(preview.revision(), 2);
        QVERIFY(!preview.valid()); QVERIFY(!preview.pageValid(1)); QVERIFY(preview.busy());
        auto request = last(commands);
        QCOMPARE(request[0].toString(), QString("preview-render"));
        QCOMPARE(request[2].toInt(), 0); QCOMPARE(request[4].toInt(), 2);
        deliver(preview, request, Qt::green); QTRY_VERIFY(preview.valid());
        QTRY_COMPARE(last(commands)[0].toString(), QString("preview-background"));
        request = last(commands);
        QCOMPARE(request[2].toInt(), 1); QCOMPARE(request[4].toInt(), 2); QCOMPARE(request[5].toInt(), 0);
        deliver(preview, request, Qt::yellow); QTRY_VERIFY(preview.pageValid(1));
        QCOMPARE(preview.image().pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(preview.pageImage(1).pixelColor(0, 0), QColor(Qt::yellow));
        deliver(preview, oldSecondary, Qt::blue); QTest::qWait(20);
        QCOMPARE(preview.pageImage(1).pixelColor(0, 0), QColor(Qt::yellow));
    }
}
void ScrollingTest::realContinuousViewer()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for continuous viewer integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray source = "\\documentclass{article}\n\\begin{document}\n";
    for (int page = 0; page < 8; ++page) {
        source += "Visible page " + QByteArray::number(page + 1) + ".\n";
        if (page != 7) source += "\\newpage\n";
    }
    source += "\\end{document}\n"; file.write(source); file.close();
    TexpressoManager manager(nullptr, engine);
    PDFDocumentConfig config{}; config.editTool = 2; config.zoomStepFactor = std::sqrt(2.0);
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.activate(root);
    auto session = manager.current(); session->setAutoRerun(false); session->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid(), 15000);
    auto area = widget.findChild<QScrollArea *>(); QVERIFY(area);
    auto canvas = widget.findChild<TexpressoCanvas *>("texpressoActivePage"); QVERIFY(canvas);
    QVERIFY(widget.viewerActions()["continuous"]->isChecked());
    const int stride = canvas->height() + 12;
    area->verticalScrollBar()->setValue(stride + 90);
    QTRY_COMPARE_WITH_TIMEOUT(session->preview.page(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid(), 15000);
    // Zoom retains the document point under the viewport center.
    const int before = area->verticalScrollBar()->value();
    widget.viewerActions()["actualSize"]->trigger();
    QTRY_VERIFY(session->preview.valid()); QVERIFY(area->verticalScrollBar()->value() > 0);
    QVERIFY(before > 0);
    for (int page = 2; page < 8; ++page) {
        session->preview.setPage(page);
        QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && session->preview.page() == page, 15000);
        QTRY_COMPARE(canvas->property("texpressoPage").toInt(), page);
    }
    int cached = 0;
    for (int page = 0; page < 8; ++page) if (session->preview.pageValid(page)) ++cached;
    QVERIFY(cached <= 4); QVERIFY(widget.findChildren<TexpressoCanvas *>().size() <= 4);
    QSignalSpy locations(&manager, &TexpressoManager::sourceLocation);
    QSignalSpy reveals(&session->preview, &TexpressoPreview::reveal);
    session->preview.forward(session->root(), 4); QTRY_VERIFY_WITH_TIMEOUT(!reveals.isEmpty(), 15000);
    QTRY_VERIFY(session->preview.valid());
    session->preview.inverse(reveals.last()[0].toPointF());
    QTRY_VERIFY_WITH_TIMEOUT(!locations.isEmpty(), 15000);
    QCOMPARE(QFileInfo(locations.last()[0].toString()).canonicalFilePath(), QFileInfo(root).canonicalFilePath());
    const int page = session->preview.page(); const int offset = area->verticalScrollBar()->value();
    manager.openBuffer(root, QString::fromUtf8(source).replace("Visible page", "Changed page"));
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.sequence() > 0 && session->preview.valid(), 15000);
    QCOMPARE(session->preview.page(), page);
    QVERIFY(qAbs(area->verticalScrollBar()->value() - offset) <= 3);
    // At thumbnail scale the viewport contains more than four pages. The
    // demand-dependent entry limit must retain every visible page, without
    // changing the ordinary four-entry cache budget tested by ClientTest.
    widget.resize(640, 1500);
    auto slider = widget.findChild<QSlider *>(); QVERIFY(slider); slider->setValue(-100);
    const auto visibleCanvases = [&] {
        QList<TexpressoCanvas *> result;
        for (auto item : widget.findChildren<TexpressoCanvas *>()) {
            const QRect inViewport(item->mapTo(area->viewport(), QPoint()), item->size());
            if (item->isVisible() && inViewport.intersects(area->viewport()->rect())) result.append(item);
        }
        return result;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleCanvases().size() > 4, 15000);
    const auto allVisibleFresh = [&] {
        const auto visible = visibleCanvases();
        if (visible.size() <= 4) return false;
        for (auto item : visible) if (!item->fresh || item->pixels.isNull()) return false;
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(allVisibleFresh(), 15000);
    QVERIFY(widget.findChildren<TexpressoCanvas *>().size() <= 16);
    const int thumbnailPage = session->preview.page();
    widget.viewerActions()["continuous"]->setChecked(false);
    QTRY_VERIFY(canvas->isVisible()); QCOMPARE(session->preview.page(), thumbnailPage);
    manager.shutdown();
}
QTEST_MAIN(ScrollingTest)
#include "scrolling_test.moc"
