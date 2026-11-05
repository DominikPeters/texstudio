#include <QtTest>
#include "texpressopreview.h"
#include "ViewerControls.h"
#include <QBuffer>

class SourceRoutingTest : public QObject
{
    Q_OBJECT
private slots:
    void preciseForwardAndInverse();
    void sourceColumnsWordsAndMasks();
    void targetWaitsForItsDecodedFrame();
    void cachedTargetRevealsWithSourceFlag();
    void navigationCancelsForegroundCredit_data();
    void navigationCancelsForegroundCredit();
    void samePageNavigationRestartsRetiredExport();
    void layoutCannotOverridePendingSourceSearch();
    void decreasingRevisionCannotInvalidateCurrentFrame();
    void editRejectsOldSearchAndOldFrame();
    void completedSearchWithoutTargetUnblocksSpeculation();
    void slowSearchSettlesOnCompletionAndAcceptsLateTarget();
    void invalidationCannotReuseOldCompletion();
    void sourceEpochCannotReuseOldCompletion();
    void navigationBounds_data();
    void navigationBounds();
    void identifiedSearchRejectsReplacedIntent();
    void identifiedSearchCancellationSettlesImmediately();
    void cancelledRenderReleasesRequestCredit();
};
static QJsonArray lastCommand(const QSignalSpy &commands)
{
    return commands.last()[0].toJsonArray();
}
static void deliver(TexpressoPreview &preview, const QJsonArray &request, Qt::GlobalColor color = Qt::white)
{
    QImage image(16, 24, QImage::Format_RGB32); image.fill(color);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    preview.receive({{"requestId", request[1]}, {"revision", request[4]}, {"sequence", request[5]},
        {"page", request[2]}, {"width", 16}, {"height", 24}, {"bounds", QJsonArray{10, 20, 170, 260}},
        {"mime", "image/png"}, {"byteLength", png.size()}}, png);
}
static void start(TexpressoPreview &preview)
{
    preview.setActive(true); preview.message({"preview-state", 10, 0, 8, true, 0});
}
void SourceRoutingTest::identifiedSearchRejectsReplacedIntent()
{
    TexpressoPreview preview;
    QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal);
    preview.setCapabilities({{"requestLifecycle", true}});
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 10);
    const auto first = lastCommand(commands);
    QCOMPARE(first[0].toString(), QString("preview-search"));
    QCOMPARE(first[2].toString(), QString("main.tex")); QCOMPARE(first[3].toInt(), 11); QCOMPARE(first[4].toInt(), 0);
    preview.forward("chapter.tex", 20);
    const auto second = lastCommand(commands); QVERIFY(second[1] != first[1]);
    preview.message({"preview-cancelled", first[1]});
    preview.message({"preview-source", first[1], 10, 0, 5, 50, 80});
    preview.message({"preview-forward", 10, 0, 5, 50, 80}); // An unowned legacy reply is ineligible.
    QCOMPARE(preview.page(), 0); QVERIFY(preview.followingSource()); QVERIFY(reveals.isEmpty());
    preview.message({"preview-source", second[1], 10, 0, 1.5, 50, 80});
    QCOMPARE(preview.page(), 0);
    preview.message({"preview-source", second[1], 10, 0, 1, 50, 80});
    QCOMPARE(preview.page(), 1);
    deliver(preview, lastCommand(commands), Qt::blue); QTRY_COMPARE(reveals.size(), 1);
    preview.forward("main.tex", 30); const auto abandoned = lastCommand(commands);
    preview.navigateTo(2);
    preview.message({"preview-source", abandoned[1], 10, 0, 3, 50, 80});
    QCOMPARE(preview.page(), 2); QCOMPARE(reveals.size(), 1); QVERIFY(!preview.followingSource());
}
void SourceRoutingTest::identifiedSearchCancellationSettlesImmediately()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"requestLifecycle", true}, {"backgroundRendering", true}});
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.setViewportPages({0}); preview.forward("definitions.tex", 10);
    const auto search = lastCommand(commands); QVERIFY(preview.followingSource());
    preview.message({"preview-cancelled", search[1]});
    QVERIFY(!preview.followingSource()); QVERIFY(preview.valid());
    QTRY_COMPARE(lastCommand(commands)[0].toString(), QString("preview-background"));
}
void SourceRoutingTest::cancelledRenderReleasesRequestCredit()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"requestLifecycle", true}}); start(preview);
    const auto old = lastCommand(commands);
    preview.message({"preview-cancelled", old[1]});
    const auto replacement = lastCommand(commands);
    QVERIFY(old[1] != replacement[1]); QCOMPARE(replacement[2].toInt(), 0);
    preview.message({"preview-cancelled", old[1]}); QCOMPARE(lastCommand(commands), replacement);
    deliver(preview, replacement, Qt::green); QTRY_VERIFY(preview.valid());
}
void SourceRoutingTest::targetWaitsForItsDecodedFrame()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    bool currentGeometry = false;
    connect(&preview, &TexpressoPreview::reveal, this, [&](const QPointF &point) {
        currentGeometry = preview.revealingSource() && preview.followingSource() && preview.valid()
            && preview.page() == 3 && preview.bounds() == QRectF(10, 20, 160, 240) && point == QPointF(70, 90);
    });
    preview.forward("main.tex", 12); QVERIFY(preview.followingSource());
    preview.message({"preview-forward", 10, 0, 3, 70, 90});
    QCOMPARE(preview.page(), 3); QVERIFY(!preview.valid()); QVERIFY(reveals.isEmpty());
    const auto request = lastCommand(commands); QCOMPARE(request[0].toString(), QString("preview-render"));
    deliver(preview, request, Qt::green);
    // Decode is asynchronous: even after receiving bytes, old geometry cannot
    // move the viewport before the new image and bounds have been published.
    QVERIFY(reveals.isEmpty());
    QTRY_COMPARE(reveals.size(), 1); QVERIFY(currentGeometry);
    QVERIFY(!preview.revealingSource()); QVERIFY(!preview.followingSource());
}
void SourceRoutingTest::cachedTargetRevealsWithSourceFlag()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.setPage(1); deliver(preview, lastCommand(commands), Qt::blue); QTRY_VERIFY(preview.valid());
    preview.setPage(0); QVERIFY(preview.valid());
    bool source = false;
    connect(&preview, &TexpressoPreview::reveal, this, [&](const QPointF &) { source = preview.revealingSource() && preview.valid() && preview.page() == 1; });
    preview.forward("main.tex", 10); preview.message({"preview-forward", 10, 0, 1, 50, 80});
    QCOMPARE(reveals.size(), 1); QVERIFY(source); QCOMPARE(preview.image().pixelColor(0, 0), QColor(Qt::blue));
}
void SourceRoutingTest::navigationCancelsForegroundCredit_data()
{
    QTest::addColumn<bool>("scroll");
    QTest::newRow("page-button") << false;
    QTest::newRow("manual-scroll") << true;
}
void SourceRoutingTest::navigationCancelsForegroundCredit()
{
    QFETCH(bool, scroll);
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal), manual(&preview, &TexpressoPreview::manualNavigated);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 10); preview.message({"preview-forward", 10, 0, 5, 50, 80});
    const auto obsolete = lastCommand(commands); QCOMPARE(obsolete[2].toInt(), 5);
    if (scroll) preview.setViewportPages({2}, true);
    else preview.navigateTo(2);
    QCOMPARE(manual.size(), 1); QVERIFY(!preview.followingSource()); QCOMPARE(preview.page(), 2);
    const auto current = lastCommand(commands); QCOMPARE(current[0].toString(), QString("preview-render"));
    QCOMPARE(current[2].toInt(), 2); QVERIFY(current[1] != obsolete[1]);
    bool cancelled = false;
    for (const auto &entry : commands) {
        const auto c = entry[0].toJsonArray();
        if (c[0] == "preview-cancel" && c[1].toInt(-1) == 0 && c[2].toInt(-1) == 2) cancelled = true;
    }
    QVERIFY(cancelled);
    preview.message({"preview-forward", 10, 0, 5, 50, 80}); // Same epoch, abandoned user intent.
    deliver(preview, obsolete, Qt::red); QTest::qWait(20);
    QCOMPARE(preview.page(), 2); QVERIFY(reveals.isEmpty());
    deliver(preview, current, Qt::green); QTRY_VERIFY(preview.valid());
    QCOMPARE(preview.image().pixelColor(0, 0), QColor(Qt::green)); QVERIFY(reveals.isEmpty());
}
void SourceRoutingTest::samePageNavigationRestartsRetiredExport()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 10); preview.setWidth(777);
    const auto obsolete = lastCommand(commands); QCOMPARE(obsolete[0].toString(), QString("preview-render"));
    preview.navigateTo(0);
    const auto current = lastCommand(commands); QCOMPARE(current[0].toString(), QString("preview-render"));
    QVERIFY(current[1] != obsolete[1]); QCOMPARE(current[2].toInt(), 0);
    deliver(preview, obsolete, Qt::red); deliver(preview, current, Qt::blue);
    QTRY_COMPARE(preview.image().pixelColor(0, 0), QColor(Qt::blue)); QVERIFY(!preview.followingSource());
}
void SourceRoutingTest::layoutCannotOverridePendingSourceSearch()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 10);
    preview.setViewportPages({2}, false); QCOMPARE(preview.page(), 0); QVERIFY(preview.followingSource());
    preview.message({"preview-forward", 10, 0, 5, 50, 80});
    const auto current = lastCommand(commands); QCOMPARE(current[2].toInt(), 5);
    preview.setViewportPages({0}, false); QCOMPARE(preview.page(), 5); QVERIFY(preview.followingSource());
    deliver(preview, current); QTRY_COMPARE(reveals.size(), 1); QCOMPARE(preview.page(), 5);
}
void SourceRoutingTest::decreasingRevisionCannotInvalidateCurrentFrame()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    const auto image = preview.image(); const auto count = commands.size();
    preview.message({"preview-state", 9, 0, 2, false, 1});
    preview.message({"preview-invalidated", 9, 0});
    QCOMPARE(preview.revision(), 10); QCOMPARE(preview.pageCount(), 8); QVERIFY(preview.complete()); QVERIFY(preview.valid());
    QCOMPARE(preview.image(), image); QCOMPARE(commands.size(), count);
}
void SourceRoutingTest::editRejectsOldSearchAndOldFrame()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 10); preview.message({"preview-forward", 10, 0, 5, 50, 80});
    const auto obsolete = lastCommand(commands);
    preview.sourceChanged(1);
    preview.message({"preview-forward", 10, 0, 6, 50, 80}); deliver(preview, obsolete, Qt::red);
    QTest::qWait(20); QVERIFY(reveals.isEmpty()); QVERIFY(!preview.followingSource());
    preview.message({"preview-state", 11, 1, 8, true, 0});
    const auto current = lastCommand(commands); QCOMPARE(current[4].toInt(), 11); QCOMPARE(current[5].toInt(), 1);
    deliver(preview, current, Qt::green); QTRY_VERIFY(preview.valid()); QVERIFY(reveals.isEmpty());
}
void SourceRoutingTest::completedSearchWithoutTargetUnblocksSpeculation()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), manual(&preview, &TexpressoPreview::manualNavigated);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.setCapabilities({{"backgroundRendering", true}});
    preview.forward("definitions.tex", 10); preview.setViewportPages({0});
    QVERIFY(preview.followingSource());
    QTRY_VERIFY_WITH_TIMEOUT(!preview.followingSource(), 3000);
    QVERIFY(!preview.busy()); QCOMPARE(manual.size(), 0);
    QTRY_COMPARE(lastCommand(commands)[0].toString(), QString("preview-background"));
}
void SourceRoutingTest::slowSearchSettlesOnCompletionAndAcceptsLateTarget()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal), manual(&preview, &TexpressoPreview::manualNavigated);
    preview.setActive(true); preview.message({"preview-state", 10, 0, 2, false, 0});
    deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.setCapabilities({{"backgroundRendering", true}});
    preview.forward("main.tex", 1000); preview.setViewportPages({0});
    QTest::qWait(2200);
    QVERIFY(preview.followingSource());
    QCOMPARE(lastCommand(commands)[0].toString(), QString("synctex-forward"));
    preview.message({"preview-state", 10, 0, 2, true, 0});
    QVERIFY(!preview.followingSource());
    // Completion may settle a source with no records. A reply already queued
    // for the same source intent remains valid and must still reveal its frame.
    preview.message({"preview-forward", 10, 0, 1, 50, 80});
    const auto request = lastCommand(commands); QCOMPARE(request[0].toString(), QString("preview-render"));
    deliver(preview, request, Qt::blue);
    QTRY_COMPARE(reveals.size(), 1); QCOMPARE(preview.page(), 1); QCOMPARE(manual.size(), 0);
}
void SourceRoutingTest::invalidationCannotReuseOldCompletion()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 1000);
    preview.message({"preview-invalidated", 11, 0});
    QTest::qWait(2200);
    QVERIFY(preview.followingSource()); // complete=true still belongs to revision 10.
    preview.message({"preview-state", 11, 0, 1, false, 0});
    QVERIFY(preview.followingSource());
    preview.message({"preview-state", 11, 0, 1, true, 0});
    QVERIFY(!preview.followingSource());
}
void SourceRoutingTest::navigationBounds_data()
{
    QTest::addColumn<bool>("complete");
    QTest::newRow("complete-document") << true;
    QTest::newRow("provisional-document") << false;
}
void SourceRoutingTest::navigationBounds()
{
    QFETCH(bool, complete);
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command), manual(&preview, &TexpressoPreview::manualNavigated);
    preview.setActive(true); preview.message({"preview-state", 10, 0, 8, complete, 0});
    deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    const auto hasCancel = [&](int page) {
        for (const auto &entry : commands) {
            const auto command = entry[0].toJsonArray();
            if (command[0] == "preview-cancel" && command[1] == 0 && command[2] == page) return true;
        }
        return false;
    };
    preview.forward("main.tex", 10); preview.navigateTo(-1);
    QCOMPARE(preview.page(), 0); QVERIFY(hasCancel(0));
    // An unknown total permits one discovery page, rather than materializing
    // an arbitrary distant page. Last Page explicitly requests EOF instead.
    const int last = complete ? 7 : 8;
    preview.forward("main.tex", 10); preview.navigateTo(100001);
    QCOMPARE(preview.page(), last); QVERIFY(hasCancel(last));
    preview.forward("main.tex", 10);
    const auto commandCount = commands.size(), manualCount = manual.size();
    preview.setViewportPages({-1, 100001}, false);
    preview.setViewportPages({-1, 100001}, true);
    QCOMPARE(preview.page(), last); QCOMPARE(commands.size(), commandCount); QCOMPARE(manual.size(), manualCount);
    QVERIFY(preview.followingSource());
}
void SourceRoutingTest::sourceEpochCannotReuseOldCompletion()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.sourceChanged(1); preview.forward("main.tex", 1000);
    QTest::qWait(2200);
    QVERIFY(preview.followingSource()); // No state has acknowledged this source epoch.
    preview.message({"preview-state", 11, 1, 1, true, 0});
    QVERIFY(!preview.followingSource());
}
void SourceRoutingTest::sourceColumnsWordsAndMasks()
{
    const auto line = QString::fromUtf8("é😀 office café office");
    QCOMPARE(ViewerControls::utf8SourceColumn(line, 1), 3);
    QCOMPARE(ViewerControls::utf8SourceColumn(line, 2), 3); // Mid-surrogate snaps back.
    QCOMPARE(ViewerControls::utf8SourceColumn(line, 3), 7);
    QCOMPARE(ViewerControls::utf16SourceColumn(line, 5), 1); // Mid-UTF8 scalar snaps back.
    QCOMPARE(ViewerControls::utf16SourceColumn(line, 7), 3);
    QCOMPARE(ViewerControls::utf16SourceColumn(line, 100), -1);
    QCOMPARE(ViewerControls::sourceWord(line, 17), QString("office"));
    QCOMPARE(ViewerControls::sourceWord(QString::fromUtf8("cafe\u0301"), 3), QString::fromUtf8("café"));
    QVERIFY(ViewerControls::sourceWord("\\section{hello}", 5).isEmpty());
    QCOMPARE(ViewerControls::sourceWordColumn({4, 16}, 6), -1);
    QCOMPARE(ViewerControls::sourceWordColumn({4, 16}, 6, 17), 19);
    QCOMPARE(ViewerControls::sourceWordColumn({4, 16}, 6, 13), -1); // Equal-distance ambiguity.
    QCOMPARE(ViewerControls::sourceWordColumn({4}, 6), 7);
    QVERIFY(ViewerControls::matchesSyncFileMask("/tmp/figure.tikz", "*.tex; *.tikz"));
    QVERIFY(!ViewerControls::matchesSyncFileMask("/tmp/figure.sty", "*.tex;*.tikz"));
    QVERIFY(!ViewerControls::matchesSyncFileMask("/tmp/figure.TEX", "*.tex"));
    QVERIFY(ViewerControls::matchesSyncFileMask("/tmp/main.tex.backup", "*.tex")); // Existing unanchored PDF semantics.
    QVERIFY(ViewerControls::matchesSyncFileMask("/tmp/anything.sty", ""));
}
void SourceRoutingTest::preciseForwardAndInverse()
{
    TexpressoPreview preview;
    QSignalSpy commands(&preview, &TexpressoPreview::command), reveals(&preview, &TexpressoPreview::reveal),
        highlights(&preview, &TexpressoPreview::sourceHighlights), locations(&preview, &TexpressoPreview::sourceLocation);
    preview.setCapabilities({{"requestLifecycle", true}, {"synctexPrecision", 1}});
    start(preview); deliver(preview, lastCommand(commands)); QTRY_VERIFY(preview.valid());
    preview.forward("main.tex", 2, 17, "office");
    const auto search = lastCommand(commands);
    QCOMPARE(search, (QJsonArray{"preview-search-precise", search[1], "main.tex", 3, 0, 17, "office"}));
    preview.message({"preview-source-precise", search[1], 10, 0, 1, 50, 80, QJsonArray{QJsonValue(QJsonArray{40, 70, 60, 90})}});
    QVERIFY(reveals.isEmpty()); // Geometry must wait for the target page's pixels.
    deliver(preview, lastCommand(commands)); QTRY_COMPARE(reveals.size(), 1);
    QCOMPARE(highlights.last()[0].toInt(), 1);
    QCOMPARE(qvariant_cast<QList<QRectF>>(highlights.last()[1]), QList<QRectF>{QRectF(40, 70, 20, 20)});
    preview.setSyncInputTags(QJsonArray{1, 4}); commands.clear();
    preview.inverse({50, 80}, false, false);
    QCOMPARE(commands.size(), 1);
    const auto inverse = lastCommand(commands);
    QCOMPARE(inverse, (QJsonArray{"preview-inverse-precise", inverse[1], 10, 0, 1, 50, 80, QJsonArray{1, 4}, false}));
    const QJsonObject hit{{"file", "main.tex"}, {"line", 3}, {"column", 17}, {"word", "office"}};
    preview.message({"preview-inverse-result", inverse[1], 9, 0, 1, hit}); QVERIFY(locations.isEmpty());
    preview.message({"preview-inverse-result", inverse[1], 10, 0, 0, hit}); QVERIFY(locations.isEmpty());
    preview.message({"preview-inverse-result", inverse[1], 10, 0, 1, hit}); QCOMPARE(locations.size(), 1);
    QVERIFY(!locations.last()[4].toBool()); QCOMPARE(commands.size(), 1); // No render or preparation commands.
    preview.inverse({50, 80}); const auto old = lastCommand(commands);
    preview.inverse({60, 80}); const auto current = lastCommand(commands);
    preview.message({"preview-inverse-result", old[1], 10, 0, 1, hit}); QCOMPARE(locations.size(), 1);
    preview.message({"preview-inverse-result", current[1], 10, 0, 1, QJsonValue::Null});
    preview.message({"preview-inverse-result", current[1], 10, 0, 1, hit}); QCOMPARE(locations.size(), 1); // Null settles.
    preview.inverse({60, 80}); const auto stale = lastCommand(commands);
    preview.sourceChanged(1);
    preview.message({"preview-inverse-result", stale[1], 10, 0, 1, hit}); QCOMPARE(locations.size(), 1);
}
QTEST_MAIN(SourceRoutingTest)
#include "source_routing_test.moc"
