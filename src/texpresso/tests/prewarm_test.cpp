#include <QtTest>
#include "texpressoprewarm.h"
#include "texpressosession.h"
#include <QFile>
#include <QTemporaryDir>
#include <QBuffer>

class TexpressoPrewarmTest : public QObject
{
    Q_OBJECT
private slots:
    void deferredResolution();
    void bucketsAndEpochs();
    void cancellationAndStaleAcknowledgments();
    void skippedRetryAfterProgress();
    void unavailableAndBusy();
    void reentrantStartedCancellation();
    void watchdogDoesNotLoop();
    void boundedAttempts();
    void utf8AliasSession();
    void inverseCursorDoesNotPrepareOrSearchAgain();
    void failedPaginationCancelsPrewarming();
    void interactionsAfterPrewarmingKeepGeneration();
};
static QJsonArray sent(const QSignalSpy &spy, int index = -1)
{
    return spy.at(index < 0 ? spy.size() - 1 : index).at(0).toJsonArray();
}
static QJsonArray reply(const QJsonArray &request, const QString &status)
{
    return {"preview-prepared", request[1], request[4], request[5], status};
}
void TexpressoPrewarmTest::deferredResolution()
{
    qint64 id = 0; int first = 0, second = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    warm.setContext({true, true, 7, 3});
    warm.select("old", [&]() -> std::optional<TexpressoPrewarm::Position> { ++first; return {{"old.tex", 4}}; });
    QCOMPARE(first, 0); QVERIFY(warm.pending());
    warm.select("latest", [&]() -> std::optional<TexpressoPrewarm::Position> { ++second; return {{"latest.tex", 83}}; });
    QCOMPARE(first, 0); QCOMPARE(second, 0);
    QTRY_COMPARE(commands.size(), 1);
    QCOMPARE(first, 0); QCOMPARE(second, 1);
    QCOMPARE(sent(commands), QJsonArray({"preview-prepare", 1, "latest.tex", 83, 7, 3}));
    warm.message(reply(sent(commands), "ready"));
    QVERIFY(!warm.pending());
    warm.refresh(); QVERIFY(!warm.pending());
    QCOMPARE(second, 1); QCOMPARE(commands.size(), 1);
}
void TexpressoPrewarmTest::bucketsAndEpochs()
{
    qint64 id = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    warm.setContext({true, true, 1, 0});
    auto select = [&](int offset) {
        warm.select(QString::number(offset), [offset]() -> std::optional<TexpressoPrewarm::Position> { return {{"main.tex", offset}}; });
    };
    select(64); QTRY_COMPARE(commands.size(), 1); warm.message(reply(sent(commands), "cached"));
    select(127); QTRY_VERIFY_WITH_TIMEOUT(!warm.pending(), 1000); QCOMPARE(commands.size(), 1);
    select(128); QTRY_COMPARE(commands.size(), 2); warm.message(reply(sent(commands), "ready"));
    warm.setContext({true, true, 2, 0}); QTRY_COMPARE(commands.size(), 3);
    QCOMPARE(sent(commands)[4].toInt(), 2); warm.message(reply(sent(commands), "ready"));
    warm.setContext({true, true, 2, 1}); QTRY_COMPARE(commands.size(), 4);
    QCOMPARE(sent(commands)[5].toInt(), 1); warm.message(reply(sent(commands), "ready"));
}
void TexpressoPrewarmTest::cancellationAndStaleAcknowledgments()
{
    qint64 id = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command), settled(&warm, &TexpressoPrewarm::settled);
    warm.setContext({true, true, 8, 2});
    warm.select("cursor", []() -> std::optional<TexpressoPrewarm::Position> { return {{"main.tex", 81}}; });
    QTRY_COMPARE(commands.size(), 1); const auto old = sent(commands);
    warm.interrupt();
    QCOMPARE(sent(commands), QJsonArray({"preview-prepare-cancel", old[1]}));
    QVERIFY(!warm.pending());
    warm.message(reply(old, "ready")); QCOMPARE(settled.size(), 0);
    warm.refresh(); QTRY_COMPARE(commands.size(), 3); const auto current = sent(commands);
    QVERIFY(current[1] != old[1]);
    warm.message(reply(old, "cancelled")); QVERIFY(warm.pending());
    auto stale = reply(current, "ready"); stale[2] = 7;
    warm.message(stale); QVERIFY(warm.pending());
    stale = reply(current, "ready"); stale[3] = 3;
    warm.message(stale); QVERIFY(warm.pending());
    warm.message(reply(current, "unexpected")); QVERIFY(warm.pending());
    warm.message(reply(current, "ready")); QVERIFY(!warm.pending()); QCOMPARE(settled.size(), 1);
}
void TexpressoPrewarmTest::skippedRetryAfterProgress()
{
    qint64 id = 0; bool known = false; int resolves = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    warm.setContext({true, true, 4, 3});
    warm.select("cursor", [&]() -> std::optional<TexpressoPrewarm::Position> {
        ++resolves; return known ? std::optional<TexpressoPrewarm::Position>{{"include", 75}} : std::nullopt;
    });
    QTRY_COMPARE(resolves, 1); QVERIFY(commands.isEmpty()); QVERIFY(!warm.pending());
    known = true; warm.refresh(); QVERIFY(!warm.pending()); QCOMPARE(resolves, 1);
    warm.refresh(true); QTRY_COMPARE(commands.size(), 1); QCOMPARE(resolves, 2);
    warm.message(reply(sent(commands), "skipped"));
    warm.refresh(); QVERIFY(!warm.pending()); QCOMPARE(commands.size(), 1);
    warm.refresh(true); QTRY_COMPARE(commands.size(), 2);
    warm.message(reply(sent(commands), "ready"));
    warm.refresh(true); QVERIFY(!warm.pending()); QCOMPARE(commands.size(), 2);
}
void TexpressoPrewarmTest::unavailableAndBusy()
{
    qint64 id = 0; int resolves = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    warm.select("cursor", [&]() -> std::optional<TexpressoPrewarm::Position> { ++resolves; return {{"main.tex", 64}}; });
    warm.setContext({false, true, 2, 0}); QVERIFY(!warm.pending());
    warm.setContext({true, false, 2, 0}); QVERIFY(!warm.pending());
    QCOMPARE(resolves, 0); QVERIFY(commands.isEmpty());
    warm.setContext({true, true, 2, 0}); QVERIFY(warm.pending());
    warm.setContext({true, false, 2, 0});
    QCOMPARE(resolves, 0); QVERIFY(!warm.pending());
    warm.setContext({true, true, 2, 0}); QTRY_COMPARE(commands.size(), 1);
    warm.setContext({false, true, 2, 0});
    QCOMPARE(sent(commands)[0].toString(), QString("preview-prepare-cancel")); QVERIFY(!warm.pending());
}
void TexpressoPrewarmTest::reentrantStartedCancellation()
{
    qint64 id = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    connect(&warm, &TexpressoPrewarm::started, &warm, [&] { warm.setContext({false, false, 1, 0}); });
    warm.setContext({true, true, 1, 0});
    warm.select("cursor", []() -> std::optional<TexpressoPrewarm::Position> { return {{"main.tex", 64}}; });
    QTRY_VERIFY(!commands.isEmpty());
    QCOMPARE(commands.size(), 1);
    QCOMPARE(sent(commands)[0].toString(), QString("preview-prepare-cancel"));
    QVERIFY(!warm.pending());
}
void TexpressoPrewarmTest::watchdogDoesNotLoop()
{
    qint64 id = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command), settled(&warm, &TexpressoPrewarm::settled);
    warm.setContext({true, true, 1, 0});
    warm.select("cursor", []() -> std::optional<TexpressoPrewarm::Position> { return {{"main.tex", 64}}; });
    QTRY_COMPARE(commands.size(), 1); const auto request = sent(commands);
    QTRY_VERIFY_WITH_TIMEOUT(!warm.pending(), 6500);
    QCOMPARE(commands.size(), 2);
    QCOMPARE(sent(commands), QJsonArray({"preview-prepare-cancel", request[1]}));
    QCOMPARE(settled.size(), 1);
    warm.message(reply(request, "ready")); QCOMPARE(settled.size(), 1);
    warm.refresh(true); QVERIFY(!warm.pending()); QCOMPARE(commands.size(), 2);
    warm.reset(); warm.refresh(); QTRY_COMPARE(commands.size(), 3);
    warm.message(reply(sent(commands), "ready"));
}
void TexpressoPrewarmTest::boundedAttempts()
{
    qint64 id = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    warm.setContext({true, true, 1, 0});
    auto select = [&](int bucket) {
        warm.select(QString::number(bucket), [bucket]() -> std::optional<TexpressoPrewarm::Position> { return {{"main.tex", bucket * 64}}; });
    };
    for (int i = 0; i < 17; ++i) {
        select(i); QTRY_COMPARE(commands.size(), i + 1);
        warm.message(reply(sent(commands), "cached"));
    }
    select(0); QTRY_COMPARE(commands.size(), 18); // Oldest attempt was evicted.
    warm.message(reply(sent(commands), "ready"));
    select(16); QTRY_VERIFY_WITH_TIMEOUT(!warm.pending(), 1000); QCOMPARE(commands.size(), 18); // Recent one remains.
}
void TexpressoPrewarmTest::utf8AliasSession()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for engine integration tests");
    QTemporaryDir temp; QVERIFY(temp.isValid());
    const auto root = temp.filePath("main.tex"), child = temp.filePath("chapter.tex");
    const QString rootText = "\\documentclass{article}\n\\begin{document}\nFirst.\\newpage\n%" + QString(2100, 'x') + "\n\\input{chapter}\n\\end{document}\n";
    const QString childText = "%" + QString(75, 'x') + " 😀\nPrépare café here. More source for the checkpoint.\n";
    for (const auto &entry : {qMakePair(root, rootText), qMakePair(child, childText)}) {
        QFile file(entry.first); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(entry.second.toUtf8()), entry.second.toUtf8().size());
    }
    const auto key = TexpressoSession::identity(child);
    TexpressoSession live(root, engine, {{TexpressoSession::identity(root), rootText}, {key, childText}});
    live.followCursor = false; live.setAutoRerun(false);
    QSignalSpy commands(&live.prewarm, &TexpressoPrewarm::command), reveals(&live.preview, &TexpressoPreview::reveal), sources(&live, &TexpressoSession::sourceLocation);
    live.setActive(true); live.start();
    QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid() || live.failed(), 20000);
    QVERIFY2(!live.failed(), qPrintable(live.engineOutput));
    if (!live.supportsPreparation) QSKIP("Engine does not advertise prepareEdit");
    live.preview.setPage(1);
    QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid() && live.preview.page() == 1 && live.inputAliases.contains(key), 15000);
    const int emoji = childText.indexOf(QString::fromUtf8("😀"));
    live.supportsPreparation = false;
    live.prepareEdit(child, 0, emoji + 1); QTest::qWait(240);
    QVERIFY(commands.isEmpty()); QVERIFY(!live.prewarm.pending());
    live.supportsPreparation = true;
    live.prepareEdit(child, 0, emoji + 1); // Synthetic cursor inside a UTF-16 pair.
    QTRY_VERIFY_WITH_TIMEOUT(!commands.isEmpty(), 10000);
    QCOMPARE(sent(commands)[3].toInt(), childText.left(emoji).toUtf8().size());
    QTRY_VERIFY_WITH_TIMEOUT(!live.prewarm.pending(), 10000);
    live.prewarm.reset(); commands.clear();
    const QString prefix = "Prépare café ";
    live.prepareEdit(child, 1, prefix.size());
    QTRY_VERIFY_WITH_TIMEOUT(!commands.isEmpty(), 10000);
    const auto request = sent(commands, 0);
    QCOMPARE(request[0].toString(), QString("preview-prepare"));
    QCOMPARE(request[2].toString(), live.inputAliases.value(key));
    QCOMPARE(request[3].toInt(), (childText.left(childText.indexOf('\n') + 1) + prefix).toUtf8().size());
    QTRY_VERIFY_WITH_TIMEOUT(!live.prewarm.pending(), 10000);
    QVERIFY(live.preview.valid());
    live.prepareEdit(child, 1, prefix.size() + 1);
    QVERIFY(live.prewarm.pending());
    live.setPaused(true); QVERIFY(!live.prewarm.pending());
    live.setPaused(false);
    const auto changed = childText.left(childText.indexOf(prefix) + prefix.size()) + "warm " + childText.mid(childText.indexOf(prefix) + prefix.size());
    live.changeBuffer(child, 1, prefix.size(), 1, prefix.size(), "warm ", changed);
    QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid() || live.failed(), 15000);
    QVERIFY2(live.preview.valid(), qPrintable(live.engineOutput));
    live.syncForward(child, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!reveals.isEmpty(), 10000);
    live.preview.inverse(reveals.last()[0].toPointF());
    QTRY_VERIFY_WITH_TIMEOUT(!sources.isEmpty(), 10000);
    QCOMPARE(TexpressoSession::identity(sources.last()[0].toString()), key);
    QCOMPARE(sources.last()[1].toInt(), 2);
    live.prepareEdit(child, 0, emoji + 1);
    live.stop(); QVERIFY(!live.prewarm.pending());
}
void TexpressoPrewarmTest::inverseCursorDoesNotPrepareOrSearchAgain()
{
    const QString root = "/tmp/inverse-cursor.tex";
    TexpressoSession live(root, {}, {{root, "first\nsecond\nthird\n"}});
    live.ready = live.foreground = live.supportsPreparation = true;
    QSignalSpy commands(&live.preview, &TexpressoPreview::command), preparations(&live.prewarm, &TexpressoPrewarm::command);
    live.preview.setActive(true); live.preview.message({"preview-state", 1, 0, 1, true, 0});
    const auto request = sent(commands);
    QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::white);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    live.preview.receive({{"requestId", request[1]}, {"revision", 1}, {"sequence", 0}, {"page", 0},
        {"bounds", QJsonArray{0, 0, 600, 800}}, {"width", 8}, {"height", 8}, {"mime", "image/png"}, {"byteLength", png.size()}}, png);
    QTRY_VERIFY(live.preview.valid()); commands.clear();
    connect(&live, &TexpressoSession::sourceLocation, &live, [&](const QString &path, int line) {
        live.prepareEdit(path, line - 1, 0); // The editor's cursorPositionChanged callback.
        live.syncForward(path, line - 1, false, 0);
    });
    live.message({"synctex", root, 2, 0, ""});
    QVERIFY(!live.prewarm.pending()); QVERIFY(commands.isEmpty()); QVERIFY(preparations.isEmpty());
    // A later activation refresh for the same caret also remains navigation.
    live.prepareEdit(root, 1, 0); live.syncForward(root, 1, false, 0);
    QVERIFY(!live.prewarm.pending()); QVERIFY(commands.isEmpty());
    live.prepareEdit(root, 1, 1); QVERIFY(live.prewarm.pending()); // Actual user movement resumes prewarming.
    live.prewarm.select(); live.syncForward(root, 2, false, 0);
    QVERIFY(!commands.isEmpty()); // Actual user navigation resumes cursor following.
}
void TexpressoPrewarmTest::failedPaginationCancelsPrewarming()
{
    const QString root = "/tmp/failed-pagination.tex";
    TexpressoSession live(root, {}, {{root, "first\nsecond\nthird\n"}});
    live.ready = live.foreground = live.supportsPreparation = true;
    QSignalSpy commands(&live.preview, &TexpressoPreview::command);
    live.preview.setActive(true); live.preview.message({"preview-state", 1, 0, 3, false, 0});
    const auto request = sent(commands);
    QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::white);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    live.preview.receive({{"requestId", request[1]}, {"revision", 1}, {"sequence", 0}, {"page", 0},
        {"bounds", QJsonArray{0, 0, 600, 800}}, {"width", 8}, {"height", 8}, {"mime", "image/png"}, {"byteLength", png.size()}}, png);
    QTRY_VERIFY(live.preview.valid());
    live.prepareEdit(root, 1, 0); QVERIFY(live.prewarm.pending());
    live.message({"preview-stopped", 1, 0, 3});
    QVERIFY(!live.prewarm.pending());
    // Cursor motion may not roll a failed prefix back to an earlier worker.
    // A real source change resets paginationStopped and permits warming again.
    live.prepareEdit(root, 2, 0); QVERIFY(!live.prewarm.pending());
    QCOMPARE(live.preview.pageCount(), 3); QVERIFY(live.preview.paginationStopped());
}
void TexpressoPrewarmTest::interactionsAfterPrewarmingKeepGeneration()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for prewarming integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex"), child = dir.filePath("chapter.tex");
    const QString rootText = "\\documentclass{article}\n\\begin{document}\nFirst.\\newpage\n%" + QString(2100, 'x')
        + "\n\\input{chapter}\n\\newpage\nThird.\\end{document}\n";
    const QString childText = "%" + QString(75, 'x') + "\nA paragraph with enough source to prepare an earlier checkpoint.\\label{target}\n";
    for (const auto &entry : {qMakePair(root, rootText), qMakePair(child, childText)}) {
        QFile file(entry.first); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(entry.second.toUtf8());
    }
    TexpressoSession live(root, engine, {{TexpressoSession::identity(root), rootText}, {TexpressoSession::identity(child), childText}});
    live.followCursor = false; live.setActive(true); live.start();
    QSignalSpy diagnostics(live.process.get(), &TexpressoProcess::diagnostic);
    QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid(), 15000);
    live.preview.navigateTo(1);
    QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid() && live.inputAliases.contains(TexpressoSession::identity(child)), 15000);
    live.preview.lastPage(); // Establish the complete, converged generation first.
    QTRY_VERIFY2_WITH_TIMEOUT(live.preview.complete() && live.preview.pageCount() == 3,
        qPrintable(QString("count=%1 complete=%2 error=%3 status=%4\n%5").arg(live.preview.pageCount())
            .arg(live.preview.complete()).arg(live.compilerError, live.status, live.engineOutput.right(4000))), 20000);
    const auto converged = [&] {
        for (const auto &event : diagnostics)
            if (event[0].toString().contains("aux byte-stable, convergence reached")) return true;
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(converged(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(live.preview.complete(), 10000);
    live.preview.navigateTo(1); QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid(), 10000);
    const auto revision = live.preview.revision(), sequence = live.preview.sequence();
    QSignalSpy prepared(live.process.get(), &TexpressoProcess::message), regions(&live.preview, &TexpressoPreview::regionReady);
    live.prepareEdit(child, 1, 15);
    QTRY_VERIFY_WITH_TIMEOUT(!live.prewarm.pending(), 10000);
    bool replayed = false;
    for (const auto &event : prepared) {
        const auto m = event[0].toJsonArray();
        if (m[0] == "preview-prepared" && m[4] == "ready") replayed = true;
    }
    QVERIFY2(replayed, "The fixture must actually restore an earlier checkpoint");
    live.preview.requestRegion({20, 20, 40, 40}, 80, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!regions.isEmpty(), 10000);
    live.preview.cancelRegion(); live.preview.navigateTo(0); QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid(), 10000);
    live.preview.navigateTo(1); QTRY_VERIFY_WITH_TIMEOUT(live.preview.valid(), 10000);
    QTest::qWait(1000);
    QCOMPARE(live.preview.sequence(), sequence); QCOMPARE(live.preview.revision(), revision);
    QCOMPARE(live.preview.pageCount(), 3); QVERIFY(live.preview.complete());
}
QTEST_MAIN(TexpressoPrewarmTest)
#include "prewarm_test.moc"
