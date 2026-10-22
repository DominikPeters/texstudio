#include <QtTest>
#include "texpressopreview.h"
#include <QBuffer>

class SourceRoutingTest : public QObject
{
    Q_OBJECT
private slots:
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
    const int last = complete ? 7 : 100000;
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
QTEST_MAIN(SourceRoutingTest)
#include "source_routing_test.moc"
