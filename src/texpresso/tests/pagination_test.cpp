#include <QtTest>
#include "texpressowidget.h"
#include <QBuffer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QNativeGestureEvent>
#include <QPinchGesture>
#include <QGestureEvent>

class PaginationTest : public QObject
{
    Q_OBJECT
private slots:
    void editForgetsExactCount();
    void previousTotalRemainsBestEstimate();
    void completionRetiresBeyondEndRequests();
    void failedFrontierStopsAndRepairs();
    void failedBeforeFirstPage();
    void stoppedPassBoundsNavigation();
    void lastPageIsBackgroundAndCancellable();
    void endSearchCancellation_data();
    void endSearchCancellation();
    void automaticRerunPreservesEndIntent();
    void lastPageWaitsForFrameAndReveals_data();
    void lastPageWaitsForFrameAndReveals();
    void geometryDoesNotFollowSpeculativeSelection();
    void realPageCounts_data();
    void realPageCounts();
    void realShrinkGrowAndFailure();
    void realAutomaticRerunsKeepExactCount();
    void fullBuildKeepsCompletedTotal_data();
    void fullBuildKeepsCompletedTotal();
    void realFailedIdlePassStops();
    void realRecoverableErrorStillShowsLastPage_data();
    void realRecoverableErrorStillShowsLastPage();
    void providedPaperLastPage();
    void replayKeepsPublishedAvailability();
    void failedPassKeepsTextGeometry();
    void trackpadZoomKeepsAnchor();
    void recognizedPinchKeepsCursorAnchor();
    void startupRerunsKeepPublishedTotal();
    void incompleteTypingKeepsPage_data();
    void incompleteTypingKeepsPage();
};
static QJsonArray last(const QSignalSpy &commands) { return commands.last()[0].toJsonArray(); }
static void deliver(TexpressoPreview &preview, const QJsonArray &request, const QJsonArray &textBounds = {})
{
    QImage image(16, 24, QImage::Format_RGB32); image.fill(Qt::white);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    QJsonObject header{{"requestId", request[1]}, {"revision", request[4]}, {"sequence", request[5]},
        {"page", request[2]}, {"width", 16}, {"height", 24}, {"bounds", QJsonArray{0, 0, 600, 800}},
        {"mime", "image/png"}, {"byteLength", bytes.size()}};
    if (!textBounds.isEmpty()) header["textBounds"] = textBounds;
    preview.receive(header, bytes);
}
void PaginationTest::editForgetsExactCount()
{
    TexpressoPreview preview;
    preview.message({"preview-state", 1, 0, 12, true, 0});
    QVERIFY(preview.complete()); QCOMPARE(preview.pageCount(), 12);
    preview.sourceChanged(1);
    QVERIFY(!preview.complete()); QCOMPARE(preview.pageCount(), 0);
    preview.message({"preview-state", 1, 0, 12, true, 0});
    QVERIFY(!preview.complete());
    preview.message({"preview-state", 2, 1, 3, true, 0});
    QCOMPARE(preview.pageCount(), 3);
    preview.message({"preview-invalidated", 3, 1});
    QVERIFY(!preview.complete()); QCOMPARE(preview.pageCount(), 0);
}
void PaginationTest::completionRetiresBeyondEndRequests()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 4, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.setViewportPages({0, 4}); const auto obsolete = last(commands);
    QCOMPARE(obsolete[0].toString(), QString("preview-background"));
    preview.message({"preview-state", 1, 0, 4, true, 0});
    deliver(preview, obsolete); QTest::qWait(20); QVERIFY(!preview.pageValid(4));
    const int before = commands.size(); QTest::qWait(250);
    for (int i = before; i < commands.size(); ++i) {
        const auto command = commands[i][0].toJsonArray();
        if (command[0] == "preview-background") QVERIFY(command[2].toInt() < 4);
    }
    preview.navigateTo(100000); QCOMPARE(preview.page(), 3);
}
void PaginationTest::previousTotalRemainsBestEstimate()
{
    TexpressoPreview preview;
    preview.message({"preview-state", 1, 0, 20, true, 0});
    QVERIFY(!preview.estimatedPageCount());
    preview.sourceChanged(1);
    QCOMPARE(preview.estimatedPageCount().value_or(-1), 20);
    preview.message({"preview-state", 2, 1, 5, false, 0});
    QCOMPARE(preview.estimatedPageCount().value_or(-1), 20);
    preview.navigateTo(19); QCOMPARE(preview.page(), 19);
    preview.message({"preview-state", 2, 1, 21, false, 19});
    QVERIFY(!preview.estimatedPageCount()); // New evidence disproves the old total.
    preview.message({"preview-state", 2, 1, 22, true, 19});
    preview.sourceChanged(2);
    QCOMPARE(preview.estimatedPageCount().value_or(-1), 22);
    preview.message({"preview-state", 3, 2, 3, true, 2});
    preview.sourceChanged(3);
    QCOMPARE(preview.estimatedPageCount().value_or(-1), 3);
}
void PaginationTest::failedFrontierStopsAndRepairs()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.setViewportPages({0, 1}); const auto frontier = last(commands);
    preview.message({"preview-error", frontier[1], "Compilation stopped before this page."});
    QVERIFY(preview.paginationStopped()); QVERIFY(!preview.complete());
    for (int i = 0; i < 20; ++i) { preview.navigateTo(1000); preview.setViewportPages({0, 1}, true); }
    QCOMPARE(preview.page(), 0);
    QTest::qWait(200); QVERIFY(!preview.busy());
    preview.sourceChanged(1);
    QVERIFY(!preview.paginationStopped()); QVERIFY(preview.canDiscoverPages());
    preview.message({"preview-state", 2, 1, 2, true, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.lastPage(); QCOMPARE(preview.page(), 1);
}
void PaginationTest::lastPageIsBackgroundAndCancellable()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.lastPage(); const auto request = last(commands);
    QVERIFY(preview.findingLastPage()); QCOMPARE(preview.page(), 0); QVERIFY(preview.valid());
    QCOMPARE(request[0].toString(), QString("preview-background"));
    QCOMPARE(request[2].toInt(), 100000);
    preview.navigateTo(0); QVERIFY(!preview.findingLastPage());
    preview.lastPage();
    preview.message({"preview-state", 1, 0, 7, true, 0});
    QVERIFY(!preview.findingLastPage()); QCOMPARE(preview.page(), 6);
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.sourceChanged(1); preview.message({"preview-state", 2, 1, 1, false, 6});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.lastPage(); preview.sourceChanged(2); QVERIFY(!preview.findingLastPage());
}
void PaginationTest::failedBeforeFirstPage()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 0, false, 0});
    const auto request = last(commands);
    preview.message({"preview-error", request[1], "No page produced."});
    QVERIFY(preview.paginationStopped()); QVERIFY(!preview.complete());
    QVERIFY(!preview.busy()); QCOMPARE(preview.pageCount(), 0);
    preview.sourceChanged(1); preview.message({"preview-state", 2, 1, 1, true, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
}
void PaginationTest::stoppedPassBoundsNavigation()
{
    TexpressoPreview preview;
    preview.message({"preview-state", 1, 0, 3, false, 0});
    preview.message({"preview-stopped", 1, 0, 3});
    QVERIFY(preview.paginationStopped()); QVERIFY(!preview.complete());
    QVERIFY(!preview.canDiscoverPages());
    preview.navigateTo(1000); QCOMPARE(preview.page(), 2);
    preview.sourceChanged(1);
    preview.message({"preview-state", 2, 1, 2, false, 0});
    for (const auto &message : {QJsonArray{"preview-stopped", 1, 0, 3},
             QJsonArray{"preview-stopped", 2, 0, 2}, QJsonArray{"preview-stopped", 1, 1, 2},
             QJsonArray{"preview-stopped", 2, 1, 3}, QJsonArray{"preview-stopped", 2, 1, "2"}}) {
        preview.message(message); QVERIFY(!preview.paginationStopped());
    }
    preview.message({"preview-stopped", 2, 1, 2}); QVERIFY(preview.paginationStopped());
    preview.message({"preview-invalidated", 3, 1}); QVERIFY(preview.canDiscoverPages());
    preview.message({"preview-state", 3, 1, 4, true, 0});
    preview.message({"preview-stopped", 3, 1, 4});
    QVERIFY(preview.complete()); QVERIFY(!preview.paginationStopped());
}
void PaginationTest::endSearchCancellation_data()
{
    QTest::addColumn<int>("interruption");
    QTest::newRow("manual-navigation") << 0;
    QTest::newRow("source-edit") << 1;
    QTest::newRow("pause-or-hidden-view") << 2;
    QTest::newRow("explicit-source-sync") << 3;
}
void PaginationTest::endSearchCancellation()
{
    QFETCH(int, interruption);
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 2, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    const auto image = preview.image();
    preview.lastPage(); const auto request = last(commands);
    switch (interruption) {
    case 0: preview.navigateTo(0); break;
    case 1: preview.sourceChanged(1); break;
    case 2: preview.setActive(false); break;
    case 3: preview.forward("main.tex", 0); break;
    }
    QVERIFY(!preview.findingLastPage()); QCOMPARE(preview.page(), 0);
    QCOMPARE(preview.image(), image);
    bool cancelled = false;
    for (const auto &event : commands) {
        const auto command = event[0].toJsonArray();
        if (command[0] == "preview-cancel" && command[1] == request[1]) cancelled = true;
    }
    QVERIFY(cancelled);
}
void PaginationTest::automaticRerunPreservesEndIntent()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.lastPage(); preview.message({"preview-state", 1, 0, 4, true, 0});
    QCOMPARE(preview.page(), 3);
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.message({"preview-invalidated", 2, 0});
    QVERIFY(preview.findingLastPage()); QCOMPARE(preview.page(), 3);
    QCOMPARE(preview.estimatedPageCount().value_or(-1), 4);
    QCOMPARE(last(commands)[0].toString(), QString("preview-background"));
    preview.message({"preview-state", 2, 0, 5, true, 3});
    QCOMPARE(preview.page(), 4);
    preview.navigateTo(0);
    preview.message({"preview-invalidated", 3, 0});
    QVERIFY(!preview.findingLastPage()); QCOMPARE(preview.page(), 0);
}
void PaginationTest::lastPageWaitsForFrameAndReveals_data()
{
    QTest::addColumn<bool>("successful");
    QTest::newRow("successful-pass") << true;
    QTest::newRow("pass-with-compiler-errors") << false;
}
void PaginationTest::lastPageWaitsForFrameAndReveals()
{
    QFETCH(bool, successful);
    TexpressoPreview preview;
    QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal);
    preview.setCapabilities({{"backgroundRendering", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.lastPage(); const auto endRequest = last(commands);
    preview.message({"preview-state", 1, 0, 4, successful, 0});
    if (!successful) preview.message({"preview-error", endRequest[1], "No more pages."});
    QCOMPARE(preview.page(), 3); QVERIFY(reveals.isEmpty());
    const auto frame = last(commands);
    QCOMPARE(frame[0].toString(), QString("preview-render")); QCOMPARE(frame[2].toInt(), 3);
    // A queued layout update still reports the old viewport before the final
    // frame arrives. It must not pull the explicit navigation back to page 1.
    preview.setViewportPages({0}); QCOMPARE(preview.page(), 3);
    deliver(preview, frame); QTRY_VERIFY(preview.valid()); QTRY_COMPARE(reveals.size(), 1);
    QCOMPARE(reveals[0][0].toPointF(), QPointF(0, 0));
    preview.navigateTo(0); deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    preview.lastPage(); // The cached final page also needs an explicit reveal.
    QTRY_COMPARE(reveals.size(), 2); QCOMPARE(preview.page(), 3);
}
void PaginationTest::geometryDoesNotFollowSpeculativeSelection()
{
    TexpressoManager manager; PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); QCoreApplication::processEvents();
    TexpressoSession session("/tmp/pagination.tex", {}, {}); widget.shown = &session;
    auto &preview = session.preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 3, false, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    widget.canvas->pixels = preview.image(); widget.canvas->pageBounds = preview.bounds();
    preview.setPage(9999); widget.layoutPage();
    QCOMPARE(widget.pageRects.size(), 4); // Three real pages, one discovery slot.
    preview.message({"preview-state", 1, 0, 3, true, 2}); widget.layoutPage();
    QCOMPARE(widget.pageRects.size(), 3);
    preview.sourceChanged(1); preview.message({"preview-state", 2, 1, 1, true, 0}); widget.layoutPage();
    QCOMPARE(widget.pageRects.size(), 1);
    preview.sourceChanged(2); preview.message({"preview-state", 3, 2, 0, true, 0}); widget.layoutPage();
    QVERIFY(widget.pageRects.isEmpty()); QVERIFY(!widget.canvas->isVisible());
    widget.shown = nullptr;
}
static bool write(const QString &path, const QByteArray &source)
{
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(source) == source.size();
}
static QByteArray document(int pages)
{
    QByteArray source = "\\documentclass{article}\n\\begin{document}\n";
    for (int page = 0; page < pages; ++page) {
        if (page) source += "\\newpage\n";
        source += "Page " + QByteArray::number(page + 1) + ".\n";
    }
    return source + "\\end{document}\n";
}
void PaginationTest::replayKeepsPublishedAvailability()
{
    TexpressoPreview preview;
    preview.message({"preview-state", 1, 0, 5, false, 2});
    preview.message({"preview-state", 1, 0, 1, false, 2});
    QCOMPARE(preview.pageCount(), 5);
    preview.message({"preview-stopped", 1, 0, 3});
    QCOMPARE(preview.pageCount(), 3); QVERIFY(preview.paginationStopped());
    preview.sourceChanged(1); preview.message({"preview-state", 2, 1, 1, false, 2});
    QCOMPARE(preview.pageCount(), 1);
}
void PaginationTest::failedPassKeepsTextGeometry()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 3, true, 0});
    deliver(preview, last(commands), {150, 10, 450, 700}); QTRY_VERIFY(preview.valid());
    preview.navigateTo(1); deliver(preview, last(commands), {100, 10, 500, 700}); QTRY_VERIFY(preview.valid());
    const auto range = preview.horizontalTextRange();
    preview.sourceChanged(1); preview.message({"preview-state", 2, 1, 1, false, 1});
    preview.message({"preview-stopped", 2, 1, 1});
    QCOMPARE(preview.page(), 1); QVERIFY(!preview.valid()); QVERIFY(!preview.pageImage(1).isNull());
    QCOMPARE(preview.horizontalTextRange(), range);
    preview.sourceChanged(2); preview.message({"preview-state", 3, 2, 1, true, 1});
    QCOMPARE(preview.page(), 0); QCOMPARE(preview.horizontalTextRange().width(), 300.0);
}
void PaginationTest::trackpadZoomKeepsAnchor()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    TexpressoManager manager; PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); QCoreApplication::processEvents();
    TexpressoSession session("/tmp/zoom.tex", {}, {}); widget.shown = &session;
    auto &preview = session.preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 3, true, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    widget.canvas->pixels = preview.image(); widget.canvas->pageBounds = preview.bounds();
    widget.setZoom(1); widget.layoutPage();
    const QPoint anchor(200, 200);
    const auto viewportAnchor = widget.canvas->mapTo(widget.area->viewport(), anchor);
    const auto pagePoint = QPointF(anchor) / widget.canvas->scale;
    QSignalSpy scales(widget.canvas, &TexpressoCanvas::scaleRequested);
    const auto windowAnchor = widget.canvas->mapTo(widget.window(), anchor);
    QPointingDevice trackpad("Test trackpad", 12345, QInputDevice::DeviceType::TouchPad,
        QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 2, 0);
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, &trackpad, 2,
        windowAnchor, windowAnchor, widget.canvas->mapToGlobal(anchor), .1, {});
    QCOMPARE(pinch.globalPosition().toPoint(), widget.canvas->mapToGlobal(anchor));
    // Exercise the native handler directly: Qt's synthetic-event delivery
    // otherwise substitutes the offscreen platform's current pointer state.
    widget.canvas->event(&pinch);
    QCOMPARE(scales.size(), 1); QCOMPARE(scales[0][1].toPoint(), anchor);
    QVERIFY(pinch.isAccepted()); QVERIFY(std::abs(preview.zoom - 1.1) < .0001);
    const auto newAnchor = widget.canvas->mapTo(widget.area->viewport(), (pagePoint * widget.canvas->scale).toPoint());
    QVERIFY2((newAnchor - viewportAnchor).manhattanLength() <= 2,
        qPrintable(QString("before=%1,%2 after=%3,%4 zoom=%5").arg(viewportAnchor.x()).arg(viewportAnchor.y()).arg(newAnchor.x()).arg(newAnchor.y()).arg(preview.zoom)));
    QCOMPARE(preview.sequence(), 0); QCOMPARE(preview.revision(), 1); QCOMPARE(preview.pageCount(), 3);
    widget.shown = nullptr;
#else
    QSKIP("Native gesture constructor requires Qt 6.2");
#endif
}
void PaginationTest::recognizedPinchKeepsCursorAnchor()
{
    TexpressoManager manager; PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.move(300, 220); widget.resize(640, 700); widget.show(); QCoreApplication::processEvents();
    TexpressoSession session("/tmp/pinch.tex", {}, {}); widget.shown = &session;
    auto &preview = session.preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 5, true, 0});
    deliver(preview, last(commands)); QTRY_VERIFY(preview.valid());
    widget.canvas->pixels = preview.image(); widget.canvas->pageBounds = preview.bounds();
    widget.setZoom(1); widget.layoutPage(); widget.showPage(2);
    widget.area->horizontalScrollBar()->setValue(80);
    const QPoint viewportAnchor(220, 260);
    const QPoint globalAnchor = widget.area->viewport()->mapToGlobal(viewportAnchor);
    const auto local = widget.canvas->mapFromGlobal(globalAnchor);
    const QPointF pagePoint = (QPointF(widget.documentCanvas->mapFromGlobal(globalAnchor))
        - widget.pageRects[2].topLeft()) / widget.canvas->scale;
    QSignalSpy scales(widget.canvas, &TexpressoCanvas::scaleRequested);
    const auto cleanup = qScopeGuard([&] { widget.shown = nullptr; });
    for (int i = 1; i <= 60; ++i) {
        QPinchGesture pinch;
#ifdef Q_OS_MACOS
        // QMacPinchGestureRecognizer uses a widget-local center and global hotspot.
        pinch.setCenterPoint(local);
#else
        pinch.setCenterPoint(globalAnchor);
#endif
        pinch.setHotSpot(globalAnchor); pinch.setTotalScaleFactor(std::pow(1.01, i <= 30 ? i : 60 - i));
        QGestureEvent gesture({&pinch}); widget.canvas->event(&gesture);
        QCoreApplication::processEvents();
        const auto actual = widget.documentCanvas->mapToGlobal(
            (QPointF(widget.pageRects[2].topLeft()) + pagePoint * widget.canvas->scale).toPoint());
        QVERIFY2((actual - globalAnchor).manhattanLength() <= 3,
            qPrintable(QString("step=%1 anchor=%2,%3 actual=%4,%5")
                .arg(i).arg(globalAnchor.x()).arg(globalAnchor.y()).arg(actual.x()).arg(actual.y())));
    }
    QCOMPARE(scales.size(), 60);
}

void PaginationTest::startupRerunsKeepPublishedTotal()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for startup pagination integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QByteArray source = "\\documentclass{article}\n\\begin{document}\n\\tableofcontents\n";
    for (int i = 0; i < 12; ++i)
        source += "\\newpage\n\\section{Section " + QByteArray::number(i) + "}\\label{s" + QByteArray::number(i) + "}See \\ref{s11}.\n";
    source += "\\end{document}\n"; QVERIFY(write(root, source));
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.openBuffer(root, QString::fromUtf8(source)); manager.activate(root);
    const auto live = manager.current(); live->followCursor = false; live->prepareEdit(root, 0, 0);
    QStringList trace; int highest = 0; bool regressed = false;
    connect(live->process.get(), &TexpressoProcess::message, &widget, [&](const QJsonArray &message) {
        if (message[0].toString().startsWith("preview-") || message[0] == "reset-sync")
            trace << QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    });
    connect(&live->preview, &TexpressoPreview::updated, &widget, [&] {
        const int total = live->preview.estimatedPageCount().value_or(live->preview.pageCount());
        if (highest >= 13 && total < highest) regressed = true;
        highest = std::max(highest, total);
    });
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 45000);
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("aux byte-stable, convergence reached"), 30000);
    QTest::qWait(1200);
    QVERIFY2(!regressed, qPrintable(trace.join('\n')));
    QCOMPARE(live->preview.pageCount(), 13); QVERIFY(live->preview.complete());
    manager.shutdown();
}

void PaginationTest::incompleteTypingKeepsPage_data()
{
    QTest::addColumn<QString>("opening"); QTest::addColumn<QString>("closing");
    QTest::newRow("unfinished-math") << QString("Let $x =") << QString(" y$.");
    QTest::newRow("unfinished-footnote") << QString("\\footnote{") << QString("A note.}");
}
void PaginationTest::incompleteTypingKeepsPage()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for typing integration");
    QFETCH(QString, opening); QFETCH(QString, closing);
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    const QString before = "\\documentclass{article}\n\\begin{document}\nFirst paragraph.\\par\nSecond paragraph.\\par\n"
        "Third paragraph.\\par\nFourth paragraph.\\par\nFifth paragraph.\\newpage\nSixth paragraph.\\par\n";
    const QString after = "\n\\section*{Seventh paragraph}Seventh paragraph.\\par\n\\newpage\nThird page.\n\\end{document}\n";
    QVERIFY(write(root, (before + after).toUtf8()));
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.openBuffer(root, before + after); manager.activate(root);
    const auto live = manager.current(); live->followCursor = false;
    QFile trace(qEnvironmentVariable("TEXPRESSO_TYPING_TRACE"));
    if (!trace.fileName().isEmpty() && trace.open(QIODevice::WriteOnly)) {
        connect(live->process.get(), &TexpressoProcess::diagnostic, &trace, [&trace](const QString &text) { trace.write(text.toUtf8()); trace.flush(); });
        connect(live->process.get(), &TexpressoProcess::message, &trace, [&trace](const QJsonArray &message) {
            trace.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'); trace.flush();
        });
    }
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == 3, 15000);
    live->preview.navigateTo(1); QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    widget.setZoom(1); widget.showPage(1);
    live->followCursor = true;
    for (int i = 0; i < opening.size(); ++i) {
        manager.changeBuffer(root, 8, i, 8, i, opening.mid(i, 1), before + opening.left(i + 1) + after);
        live->prepareEdit(root, 8, i + 1); live->syncForward(root, 8, false, i + 1);
        QTest::qWait(30);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!live->compilerError.isEmpty() || live->preview.paginationStopped(), 15000);
    // A lazy compiler may stop stepping once the page is available even
    // though TeX has reported an error. Stability must not require forcing EOF.
    QTest::qWait(500);
    QCOMPARE(live->preview.page(), 1);
    QVERIFY(!live->preview.image().isNull());
    QTRY_VERIFY(widget.pageRects.size() >= 2);
    QVERIFY(widget.area->verticalScrollBar()->value() >= widget.pageRects[1].top() - 12);
    for (int i = 0; i < closing.size(); ++i) {
        const int column = opening.size() + i;
        manager.changeBuffer(root, 8, column, 8, column, closing.mid(i, 1), before + opening + closing.left(i + 1) + after);
        live->prepareEdit(root, 8, column + 1); live->syncForward(root, 8, false, column + 1);
        QTest::qWait(30);
    }
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.valid() && live->compilerError.isEmpty(),
        qPrintable(QString("page=%1 count=%2 complete=%3 stopped=%4 busy=%5 error=%6\n%7\n%8\nstatus=%9 failed=%10 revision=%11 seq=%12")
            .arg(live->preview.page()).arg(live->preview.pageCount()).arg(live->preview.complete())
            .arg(live->preview.paginationStopped()).arg(live->preview.busy()).arg(live->compilerError)
            .arg(live->compilerOutput.right(2000)).arg(live->engineOutput.right(3000))
            .arg(live->status).arg(live->failed()).arg(live->preview.revision()).arg(live->preview.sequence())), 15000);
    QCOMPARE(live->preview.page(), 1);
    QCOMPARE(live->preview.estimatedPageCount().value_or(live->preview.pageCount()), 3);
    QTRY_VERIFY(live->compilerError.isEmpty());
    manager.shutdown();
}
void PaginationTest::realPageCounts_data()
{
    QTest::addColumn<QByteArray>("source"); QTest::addColumn<int>("expected");
    QTest::newRow("empty-document") << document(0) << 0;
    QTest::newRow("single-page") << document(1) << 1;
    QTest::newRow("multiple-pages") << document(5) << 5;
    QTest::newRow("trailing-newpage-does-not-add-page") << QByteArray(document(1)).replace("\\end{document}", "\\newpage\n\\end{document}") << 1;
    QTest::newRow("genuine-blank-final-page") << QByteArray(document(1)).replace("\\end{document}", "\\newpage\\null\\thispagestyle{empty}\n\\end{document}") << 2;
    QTest::newRow("two-columns-newpage-is-not-a-new-sheet") << QByteArray(document(3)).replace("{article}", "[twocolumn]{article}") << 2;
    QTest::newRow("book-inserts-real-blank-page") << QByteArray("\\documentclass{book}\n\\begin{document}\n\\chapter{One}First.\\chapter{Two}Second.\\end{document}\n") << 3;
}
void PaginationTest::realPageCounts()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for pagination integration");
    QFETCH(QByteArray, source); QFETCH(int, expected);
    QTemporaryDir dir; const auto root = dir.filePath("main.tex"); QVERIFY(write(root, source));
    // An independent final PDF is the page-count oracle, including truly blank
    // pages; a white raster must never be used as an end-of-document heuristic.
    const auto env = TexpressoProcess::texliveEnvironment();
    const auto tex = QStandardPaths::findExecutable("xelatex", env.value("PATH").split(QDir::listSeparator()));
    const auto pdfinfo = QStandardPaths::findExecutable("pdfinfo");
    if (tex.isEmpty() || pdfinfo.isEmpty()) QSKIP("XeLaTeX and pdfinfo are required for the independent page count");
    QProcess oracle; oracle.setWorkingDirectory(dir.path()); oracle.setProcessEnvironment(env);
    oracle.start(tex, {"-interaction=nonstopmode", "-halt-on-error", "main.tex"});
    QVERIFY(oracle.waitForFinished(30000)); QCOMPARE(oracle.exitCode(), 0);
    if (expected) {
        oracle.start(pdfinfo, {dir.filePath("main.pdf")}); QVERIFY(oracle.waitForFinished(10000));
        const auto match = QRegularExpression("Pages:\\s+(\\d+)").match(QString::fromUtf8(oracle.readAllStandardOutput()));
        QVERIFY(match.hasMatch()); QCOMPARE(match.captured(1).toInt(), expected);
    } else QVERIFY(!QFileInfo::exists(dir.filePath("main.pdf")));
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.activate(root);
    const auto live = manager.current(); live->setAutoRerun(false); live->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() >= 0, 15000);
    live->preview.lastPage();
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.complete(), 20000);
    QCOMPARE(live->preview.pageCount(), expected);
    QCOMPARE(live->preview.page(), std::max(0, expected - 1));
    if (expected) QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    QTRY_COMPARE(widget.pages.count->text(), QString::number(expected));
    QVERIFY(!widget.viewerActions()["next"]->isEnabled());
    QVERIFY(!widget.viewerActions()["last"]->isEnabled());
    QCOMPARE(widget.pageRects.size(), expected);
    if (!expected) {
        QCOMPARE(widget.status->text(), QString("No pages to preview."));
        QCOMPARE(widget.statusSeverity, TexpressoWidget::StatusPlain);
    }
    manager.shutdown();
}
void PaginationTest::realShrinkGrowAndFailure()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for pagination integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex"); QVERIFY(write(root, document(6)));
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.activate(root);
    const auto live = manager.current(); live->setAutoRerun(false); live->followCursor = false;
    const auto end = [&](int count) {
        live->preview.lastPage();
        QTRY_VERIFY_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == count, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
        QTRY_COMPARE(widget.pageRects.size(), count);
    };
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    QVERIFY(!live->preview.complete()); QTRY_VERIFY(widget.pages.count->text().endsWith('+'));
    QVERIFY(widget.viewerActions()["last"]->isEnabled());
    live->setPaused(true); QTRY_VERIFY(!widget.viewerActions()["last"]->isEnabled());
    live->setPaused(false); QTRY_VERIFY(widget.viewerActions()["last"]->isEnabled());
    end(6); QVERIFY(!QTest::currentTestFailed());
    const auto pid = live->processId();
    manager.openBuffer(root, QString::fromUtf8(document(2)));
    widget.refresh();
    QCOMPARE(widget.pages.count->text(), QString("6"));
    QVERIFY(widget.pages.count->palette().color(widget.pages.count->foregroundRole())
        != widget.pages.count->palette().color(QPalette::WindowText));
    QCOMPARE(widget.pages.validator->top(), 6);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() >= 0, 15000); end(2); QVERIFY(!QTest::currentTestFailed());
    QTRY_COMPARE(widget.pages.count->text(), QString("2"));
    QCOMPARE(widget.pages.count->foregroundRole(), QPalette::WindowText);
    manager.reconcileBuffer(root, QString::fromUtf8(document(8)));
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() >= 0, 15000); end(8); QVERIFY(!QTest::currentTestFailed());
    manager.reconcileBuffer(root, "\\documentclass{article}\n\\begin{document}\nOne.\\newpage\n\\footnote{\n");
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() >= 0, 15000); live->preview.lastPage();
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.paginationStopped(), 20000);
    QVERIFY(!live->preview.complete()); QCOMPARE(live->preview.pageCount(), 1);
    QTRY_COMPARE(widget.pageRects.size(), 1);
    QVERIFY(!widget.viewerActions()["next"]->isEnabled());
    manager.reconcileBuffer(root, QString::fromUtf8(document(3)));
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() >= 0, 15000); end(3); QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(live->processId(), pid); manager.shutdown();
}
void PaginationTest::realAutomaticRerunsKeepExactCount()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for pagination integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    const QByteArray source = "\\documentclass{article}\n\\begin{document}\n\\tableofcontents\nSee \\ref{end}.\n"
        "\\newpage\n\\section{Last}\\label{end}Done.\n\\end{document}\n";
    QVERIFY(write(root, source));
    TexpressoManager manager(nullptr, engine); manager.activate(root);
    const auto live = manager.current(); live->followCursor = false;
    QVERIFY(live->autoRerun());
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000); live->preview.lastPage();
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("aux byte-stable, convergence reached"), 30000);
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == 2,
        qPrintable(QString("page=%1 count=%2 complete=%3 finding=%4\n%5")
            .arg(live->preview.page()).arg(live->preview.pageCount()).arg(live->preview.complete())
            .arg(live->preview.findingLastPage()).arg(live->engineOutput.right(8000))), 15000);
    QVERIFY(!live->preview.findingLastPage());
    const auto revision = live->preview.revision();
    manager.openBuffer(root, QString::fromUtf8(source).replace("\\section{Last}", "\\section{Extra}Extra.\\newpage\n\\section{Last}"));
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() > revision, 15000); live->preview.lastPage();
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == 3, 30000);
    manager.shutdown();
}
void PaginationTest::fullBuildKeepsCompletedTotal_data()
{
    QTest::addColumn<bool>("prewarm");
    QTest::newRow("idle-after-build") << false;
    QTest::newRow("cursor-prewarming") << true;
}
void PaginationTest::fullBuildKeepsCompletedTotal()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for pagination integration");
    QFETCH(bool, prewarm);
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    const auto source = document(5); QVERIFY(write(root, source));
    const auto env = TexpressoProcess::texliveEnvironment();
    const auto tex = QStandardPaths::findExecutable("xelatex", env.value("PATH").split(QDir::listSeparator()));
    if (tex.isEmpty()) QSKIP("XeLaTeX required for a normal full build");
    QProcess build; build.setProcessEnvironment(env); build.setWorkingDirectory(dir.path());
    build.start(tex, {"-interaction=nonstopmode", "-halt-on-error", "main.tex"});
    QVERIFY(build.waitForFinished(30000)); QCOMPARE(build.exitCode(), 0);
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show();
    manager.beginExternalBuild(root);
    manager.openBuffer(root, QString::fromUtf8(source));
    manager.activate(root);
    auto live = manager.current(); live->followCursor = false;
    manager.endExternalBuild(root, true);
    if (prewarm) live->prepareEdit(root, 3, 0);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    const auto diagnostic = [&] {
        return QString("page=%1 count=%2 complete=%3\n%4")
            .arg(live->preview.page()).arg(live->preview.pageCount()).arg(live->preview.complete())
            .arg(live->engineOutput.split('\n').filter(QRegularExpression("\\[(?:rerun|pass|prepare-edit)\\]")).join('\n'));
    };
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.complete(), qPrintable(diagnostic()), 15000);
    QCOMPARE(live->preview.pageCount(), 5);
    QCOMPARE(live->preview.page(), 0); // Counting must not navigate to the end.
    QTRY_COMPARE(widget.pages.count->text(), QString("5"));
    QCOMPARE(widget.pages.count->foregroundRole(), QPalette::WindowText);
    QTest::qWait(500); QVERIFY(live->preview.complete());
    // A completed total is about the source, even when prewarming restores an
    // earlier checkpoint. Real edits must still invalidate it and learn a new
    // total, including shrinking documents and failed passes.
    if (prewarm) live->prepareEdit(root, 4, 0);
    QTest::qWait(500); QVERIFY(live->preview.complete());
    manager.openBuffer(root, QString::fromUtf8(document(3)));
    QVERIFY(!live->preview.complete());
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == 3, qPrintable(diagnostic()), 15000);
    manager.openBuffer(root, QString::fromUtf8(document(2)).replace("\\end{document}", "\\clearpage\\footnote{"));
    QTRY_VERIFY_WITH_TIMEOUT(!live->compilerError.isEmpty(), 15000);
    live->preview.lastPage(); // Drive the failed pass to its terminal boundary.
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.paginationStopped(), qPrintable(diagnostic()), 15000);
    QVERIFY(!live->preview.complete());
    manager.openBuffer(root, QString::fromUtf8(document(7)));
    // A repaired lazy pass need not rewrite auxiliary bytes before this page.
    // Explicit end demand confirms the new total without relying on aux dirtiness.
    live->preview.lastPage();
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == 7, qPrintable(diagnostic()), 15000);
    manager.shutdown();
}
void PaginationTest::realFailedIdlePassStops()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for pagination integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QVERIFY(write(root, QByteArray(document(3)).replace("\\end{document}", "\\clearpage\\footnote{")));
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.activate(root);
    const auto live = manager.current(); live->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    // No Last Page click or beyond-end render request: automatic EOF itself
    // must remove '+' and its extra layout placeholder on a failed pass.
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.paginationStopped(), 15000);
    QVERIFY(!live->preview.complete()); QCOMPARE(live->preview.pageCount(), 3);
    QCOMPARE(live->preview.page(), 0);
    QTRY_COMPARE(widget.pages.count->text(), QString("3"));
    QCOMPARE(widget.pages.count->foregroundRole(), QPalette::PlaceholderText);
    QTRY_COMPARE(widget.pageRects.size(), 3);
    QVERIFY(!live->compilerError.isEmpty());
    manager.openBuffer(root, QString::fromUtf8(document(2)));
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.complete() && live->preview.pageCount() == 2, 15000);
    QTRY_COMPARE(widget.pages.count->foregroundRole(), QPalette::WindowText);
    manager.shutdown();
}
void PaginationTest::providedPaperLastPage()
{
    const auto paper = qEnvironmentVariable("TEXPRESSO_TEST_PAPER");
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (paper.isEmpty() || engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_PAPER and TEXPRESSO_TEST_ENGINE for an isolated paper reproduction");
    const QFileInfo original(paper); QTemporaryDir dir;
    const auto root = dir.filePath(original.fileName());
    QVERIFY(QFile::copy(paper, root));
    for (const auto &suffix : {"aux", "bbl", "bcf", "toc", "out", "run.xml"}) {
        const auto name = original.completeBaseName() + '.' + suffix;
        const auto input = original.dir().filePath(name);
        if (QFileInfo::exists(input)) QVERIFY(QFile::copy(input, dir.filePath(name)));
    }
    QFile source(root); QVERIFY(source.open(QIODevice::ReadOnly));
    const auto text = QString::fromUtf8(source.readAll()); source.close();
    auto resources = QRegularExpression("\\\\addbibresource(?:\\[[^\\]]*\\])?\\{([^}]+)\\}").globalMatch(text);
    while (resources.hasNext()) {
        const auto name = resources.next().captured(1);
        QVERIFY(!QDir::isAbsolutePath(name) && !name.contains(".."));
        QVERIFY(QDir(dir.path()).mkpath(QFileInfo(name).path()));
        QVERIFY(QFile::copy(original.dir().filePath(name), dir.filePath(name)));
    }
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.activate(root);
    const auto live = manager.current(); live->followCursor = false;
    const auto artifactDir = qEnvironmentVariable("TEXPRESSO_TEST_PAPER_ARTIFACTS");
    const auto evidence = qScopeGuard([&] {
        if (artifactDir.isEmpty()) return;
        QDir().mkpath(artifactDir);
        write(QDir(artifactDir).filePath("engine.log"), live->engineOutput.toUtf8());
        write(QDir(artifactDir).filePath("compiler.log"), live->compilerOutput.toUtf8());
    });
    const auto diagnostic = [&] {
        return QString("page=%1 count=%2 complete=%3 stopped=%4 finding=%5 error=%6\n%7")
            .arg(live->preview.page()).arg(live->preview.pageCount()).arg(live->preview.complete())
            .arg(live->preview.paginationStopped()).arg(live->preview.findingLastPage())
            .arg(live->compilerError, live->engineOutput.right(2000));
    };
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.valid(), qPrintable(diagnostic()), 30000);
    widget.refresh(); QVERIFY(widget.viewerActions()["last"]->isEnabled());
    widget.viewerActions()["last"]->trigger();
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.complete() || live->preview.paginationStopped(), qPrintable(diagnostic()), 60000);
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.page() == live->preview.pageCount() - 1 && live->preview.valid(), qPrintable(diagnostic()), 15000);
    const int expected = qEnvironmentVariableIntValue("TEXPRESSO_TEST_PAPER_PAGES");
    if (expected > 0) QCOMPARE(live->preview.pageCount(), expected);
    if (live->preview.paginationStopped()) {
        QTRY_COMPARE(widget.pages.count->text(), QString::number(live->preview.pageCount()));
        QCOMPARE(widget.pages.count->foregroundRole(), QPalette::PlaceholderText);
    } else {
        QTRY_COMPARE(widget.pages.count->text(), QString::number(live->preview.pageCount()));
        QCOMPARE(widget.pages.count->foregroundRole(), QPalette::WindowText);
    }
    const int page = live->preview.page();
    QTRY_VERIFY(page < widget.pageRects.size());
    const int target = std::clamp(widget.pageRects[page].top() - 12, 0, widget.area->verticalScrollBar()->maximum());
    QTRY_VERIFY2_WITH_TIMEOUT(std::abs(widget.area->verticalScrollBar()->value() - target) <= 2,
        qPrintable(QString("viewport=%1 expected=%2\n%3").arg(widget.area->verticalScrollBar()->value()).arg(target).arg(diagnostic())), 5000);
    qInfo().noquote() << diagnostic(); manager.shutdown();
}
void PaginationTest::realRecoverableErrorStillShowsLastPage_data()
{
    QTest::addColumn<bool>("automatic");
    QTest::newRow("idle-auto-rerun") << true;
    QTest::newRow("explicit-last-page") << false;
}
void PaginationTest::realRecoverableErrorStillShowsLastPage()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for recovered-pass navigation integration");
    QFETCH(bool, automatic);
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QVERIFY(write(root, QByteArray(document(3)).replace("Page 1.", "\\undefinedEndNavigationCommand Page 1.")));
    TexpressoManager manager(nullptr, engine); PDFDocumentConfig config{};
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show(); manager.activate(root);
    const auto live = manager.current(); live->followCursor = false;
    live->setAutoRerun(automatic);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    if (!automatic) { widget.refresh(); widget.viewerActions()["last"]->trigger(); }
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.complete() || live->preview.paginationStopped(), 20000);
    QVERIFY(live->preview.complete()); QVERIFY(!live->preview.paginationStopped());
    QCOMPARE(live->preview.pageCount(), 3);
    QVERIFY(!live->compilerError.isEmpty()); // Errors remain visible independently of pagination.
    QTRY_COMPARE(widget.pages.count->text(), QString("3"));
    QCOMPARE(widget.pages.count->foregroundRole(), QPalette::WindowText);
    QVERIFY(!live->engineOutput.contains("aux byte-stable, convergence reached"));
    if (automatic) { QCOMPARE(live->preview.page(), 0); widget.viewerActions()["last"]->trigger(); }
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() && live->preview.page() == 2, 15000);
    const auto atEnd = [&] {
        if (widget.pageRects.size() != 3) return false;
        const int top = widget.pageRects.last().top();
        const int y = widget.area->verticalScrollBar()->value();
        return y >= top - 12 && y <= top;
    };
    QTRY_VERIFY(atEnd()); QVERIFY(!widget.viewerActions()["next"]->isEnabled());
    // The completed boundary must also work on a cached return.
    widget.viewerActions()["firstPage"]->trigger(); QTRY_COMPARE(live->preview.page(), 0);
    QTRY_VERIFY(live->preview.valid()); QTRY_VERIFY(widget.viewerActions()["last"]->isEnabled());
    widget.viewerActions()["last"]->trigger();
    QTRY_VERIFY(live->preview.valid() && live->preview.page() == 2 && atEnd());
    manager.shutdown();
}
QTEST_MAIN(PaginationTest)
#include "pagination_test.moc"
