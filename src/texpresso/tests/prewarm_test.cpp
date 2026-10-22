#include <QtTest>
#include "texpressoprewarm.h"
#include "texpressosession.h"
#include <QFile>
#include <QTemporaryDir>

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
    QTest::qWait(30);
    warm.select("latest", [&]() -> std::optional<TexpressoPrewarm::Position> { ++second; return {{"latest.tex", 83}}; });
    QCOMPARE(first, 0); QCOMPARE(second, 0);
    QTRY_COMPARE(commands.size(), 1);
    QCOMPARE(first, 0); QCOMPARE(second, 1);
    QCOMPARE(sent(commands), QJsonArray({"preview-prepare", 1, "latest.tex", 83, 7, 3}));
    warm.message(reply(sent(commands), "ready"));
    QVERIFY(!warm.pending());
    warm.refresh(); QTest::qWait(240);
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
    select(127); QTest::qWait(240); QCOMPARE(commands.size(), 1); QVERIFY(!warm.pending());
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
    known = true; warm.refresh(); QTest::qWait(240); QCOMPARE(resolves, 1);
    warm.refresh(true); QTRY_COMPARE(commands.size(), 1); QCOMPARE(resolves, 2);
    warm.message(reply(sent(commands), "skipped"));
    warm.refresh(); QTest::qWait(240); QCOMPARE(commands.size(), 1);
    warm.refresh(true); QTRY_COMPARE(commands.size(), 2);
    warm.message(reply(sent(commands), "ready"));
    warm.refresh(true); QTest::qWait(240); QCOMPARE(commands.size(), 2);
}
void TexpressoPrewarmTest::unavailableAndBusy()
{
    qint64 id = 0; int resolves = 0;
    TexpressoPrewarm warm([&] { return ++id; });
    QSignalSpy commands(&warm, &TexpressoPrewarm::command);
    warm.select("cursor", [&]() -> std::optional<TexpressoPrewarm::Position> { ++resolves; return {{"main.tex", 64}}; });
    warm.setContext({false, true, 2, 0}); QTest::qWait(240);
    warm.setContext({true, false, 2, 0}); QTest::qWait(240);
    QCOMPARE(resolves, 0); QVERIFY(commands.isEmpty());
    warm.setContext({true, true, 2, 0}); QVERIFY(warm.pending());
    warm.setContext({true, false, 2, 0}); QTest::qWait(240);
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
    QTRY_VERIFY(!commands.isEmpty()); QTest::qWait(30);
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
    warm.refresh(true); QTest::qWait(240); QCOMPARE(commands.size(), 2);
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
    select(16); QTest::qWait(240); QCOMPARE(commands.size(), 18); // Recent one remains.
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
QTEST_MAIN(TexpressoPrewarmTest)
#include "prewarm_test.moc"
