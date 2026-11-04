#include <QtTest>
#include "texpressosourcefollow.h"
#include "texpressosession.h"
#include <QFile>
#include <QTemporaryDir>
#include <functional>

class TexpressoSourceFollowTest : public QObject
{
    Q_OBJECT
private slots:
    void recursiveFlushes();
    void newSourceAndPass();
    void linesAndDocuments();
    void explicitAlwaysRequests();
    void independentSessions();
    void invalidation();
    void holdAcrossPasses();
    void selectionResumesHold();
    void holdBeforeInitialCompilation();
    void invalidationPreservesHold();
    void horizontalCaret();
    void sessionFlushDedupeAndCanonicalPath();
    void sessionStoresDisabledCaretAndKeepsEditStartSeparate();
    void sessionManualNavigationAndRecovery();
    void dirtyIncludeKeepsOwnedReveal();
private:
    void makeReady(TexpressoSession &session);
};
static TexpressoSourceFollow::Intent intent()
{
    return {"/work/main.tex", 1, 100, 0, 4, 8};
}
static int forwardCount(const QSignalSpy &commands)
{
    int count = 0;
    for (const auto &entry : commands) {
        const auto command = entry[0].toJsonArray();
        const auto verb = command[0];
        if (verb == "synctex-forward" || verb == "preview-search") ++count;
    }
    return count;
}
static QJsonArray lastForward(const QSignalSpy &commands)
{
    for (int i = commands.size() - 1; i >= 0; --i) {
        const auto command = commands[i][0].toJsonArray();
        if (command[0] == "synctex-forward" || command[0] == "preview-search") return command;
    }
    return {};
}
void TexpressoSourceFollowTest::recursiveFlushes()
{
    TexpressoSourceFollow follow; const auto source = intent(); int sends = 0;
    std::function<void()> rendered = [&] { if (follow.request(source)) { ++sends; rendered(); } };
    rendered(); for (int i = 0; i < 100; ++i) rendered();
    QCOMPARE(sends, 1);
}
void TexpressoSourceFollowTest::newSourceAndPass()
{
    TexpressoSourceFollow follow; auto source = intent();
    QVERIFY(follow.request(source)); QVERIFY(!follow.request(source));
    ++source.version; ++source.sequence;
    QVERIFY(follow.request(source)); QVERIFY(!follow.request(source));
    ++source.revision;
    QVERIFY(follow.request(source)); QVERIFY(!follow.request(source));
    ++source.sequence; QVERIFY(follow.request(source));
}
void TexpressoSourceFollowTest::linesAndDocuments()
{
    TexpressoSourceFollow follow; auto source = intent(); QVERIFY(follow.request(source));
    ++source.line; QVERIFY(follow.request(source));
    --source.line; QVERIFY(follow.request(source)); QVERIFY(!follow.request(source));
    source.document = "/work/appendix.tex"; QVERIFY(follow.request(source));
    source.document = "/work/main.tex"; QVERIFY(follow.request(source));
}
void TexpressoSourceFollowTest::explicitAlwaysRequests()
{
    TexpressoSourceFollow follow; const auto source = intent();
    QVERIFY(follow.request(source));
    QVERIFY(follow.request(source, true)); QVERIFY(follow.request(source, true));
    QVERIFY(!follow.request(source));
}
void TexpressoSourceFollowTest::independentSessions()
{
    TexpressoSourceFollow first, replacement; const auto source = intent();
    QVERIFY(first.request(source)); QVERIFY(replacement.request(source));
    QVERIFY(!first.request(source)); QVERIFY(!replacement.request(source));
}
void TexpressoSourceFollowTest::invalidation()
{
    TexpressoSourceFollow first, other; const auto source = intent();
    QVERIFY(first.request(source)); QVERIFY(other.request(source)); first.invalidate();
    QVERIFY(first.request(source)); QVERIFY(!first.request(source)); QVERIFY(!other.request(source));
}
void TexpressoSourceFollowTest::holdAcrossPasses()
{
    TexpressoSourceFollow follow; auto source = intent(); QVERIFY(follow.request(source)); follow.suspend();
    for (int i = 0; i < 4; ++i) { ++source.revision; ++source.sequence; QVERIFY(!follow.request(source)); }
    ++source.version; QVERIFY(follow.request(source)); QVERIFY(!follow.request(source));
    ++source.revision; QVERIFY(follow.request(source));
}
void TexpressoSourceFollowTest::selectionResumesHold()
{
    TexpressoSourceFollow follow; auto source = intent();
    QVERIFY(follow.request(source)); follow.suspend(); ++source.line; QVERIFY(follow.request(source));
    follow.suspend(); source.document = "/work/appendix.tex"; QVERIFY(follow.request(source));
    follow.suspend(); QVERIFY(follow.request(source, true)); QVERIFY(!follow.request(source));
}
void TexpressoSourceFollowTest::holdBeforeInitialCompilation()
{
    TexpressoSourceFollow follow; auto source = intent(); follow.suspend();
    QVERIFY(!follow.request(source)); ++source.revision; QVERIFY(!follow.request(source));
    ++source.line; QVERIFY(follow.request(source));
}
void TexpressoSourceFollowTest::invalidationPreservesHold()
{
    TexpressoSourceFollow first, other; auto source = intent();
    QVERIFY(first.request(source)); QVERIFY(other.request(source)); first.suspend();
    ++source.revision; QVERIFY(!first.request(source)); QVERIFY(other.request(source));
    first.invalidate(); QVERIFY(!first.request(source)); QVERIFY(first.request(source, true));
}
void TexpressoSourceFollowTest::horizontalCaret()
{
    TexpressoSourceFollow follow; auto source = intent(); source.column = 2;
    QVERIFY(follow.request(source)); source.column = 3; QVERIFY(!follow.request(source));
    follow.suspend(); ++source.revision; QVERIFY(!follow.request(source));
    source.column = 4; QVERIFY(follow.request(source)); QVERIFY(!follow.request(source));
}
void TexpressoSourceFollowTest::makeReady(TexpressoSession &session)
{
    session.ready = true; session.foreground = true;
    session.preview.setActive(true);
    session.preview.message({"preview-state", 8, double(session.preview.sequence()), 1, false, 0});
    session.acknowledgedSource = session.preview.sequence();
}
void TexpressoSourceFollowTest::sessionFlushDedupeAndCanonicalPath()
{
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Source.\n"); file.close();
    const auto alias = temp.filePath("alias.tex"); QVERIFY(QFile::link(root, alias));
    TexpressoSession session(root, {}, {{TexpressoSession::identity(root), "Source.\n"}}); makeReady(session);
    QSignalSpy commands(&session.preview, &TexpressoPreview::command);
    session.syncForward(root, 0, false, 2); QCOMPARE(forwardCount(commands), 1);
    for (int i = 0; i < 100; ++i) session.message({"flush"});
    QCOMPARE(forwardCount(commands), 1);
    session.syncForward(alias, 0, false, 3); QCOMPARE(forwardCount(commands), 1);
    QCOMPARE(session.syncColumn, 3); QCOMPARE(session.syncFile, TexpressoSession::identity(root));
    session.syncForward(alias, 0); QCOMPARE(forwardCount(commands), 2);
    session.message({"flush"}); QCOMPARE(forwardCount(commands), 2);
}
void TexpressoSourceFollowTest::sessionStoresDisabledCaretAndKeepsEditStartSeparate()
{
    TexpressoSession session("/work/main.tex", {}, {{"/work/main.tex", "Source.\n"}}); makeReady(session);
    QSignalSpy commands(&session.preview, &TexpressoPreview::command);
    session.followCursor = false; session.syncForward("/work/main.tex", 17, false, 6);
    QCOMPARE(forwardCount(commands), 0); QCOMPARE(session.syncLine, 17); QCOMPARE(session.syncColumn, 6);
    session.ready = false; // No real compiler in this deterministic client test.
    session.changeBuffer("/work/main.tex", 0, 0, 0, 0, "Edit ", "Edit Source.\n");
    QCOMPARE(session.syncLine, 17); QCOMPARE(session.syncColumn, 6);
    session.ready = true; session.followCursor = true;
    session.message({"flush"}); QCOMPARE(forwardCount(commands), 1);
    QCOMPARE(lastForward(commands)[2].toInt(), 18);
}
void TexpressoSourceFollowTest::sessionManualNavigationAndRecovery()
{
    TexpressoSession session("/work/main.tex", {}, {{"/work/main.tex", "Source.\n"}}); makeReady(session);
    QSignalSpy commands(&session.preview, &TexpressoPreview::command);
    session.syncForward("/work/main.tex", 0, false, 2); QCOMPARE(forwardCount(commands), 1);
    session.preview.navigateTo(0);
    ++session.bufferVersions[session.root()]; // Editing is a real new selection.
    session.message({"flush"}); QCOMPARE(forwardCount(commands), 2);
    session.preview.navigateTo(0); session.resetEngine(); makeReady(session);
    session.suppressRestartSync = false; // Exercise the hold rather than the restart guard.
    session.message({"flush"}); QCOMPARE(forwardCount(commands), 2);
    session.suppressRestartSync = true;
    session.syncForward("/work/main.tex", 0); QCOMPARE(forwardCount(commands), 3);
    QVERIFY(!session.suppressRestartSync);
    session.message({"flush"}); QCOMPARE(forwardCount(commands), 3);
}
void TexpressoSourceFollowTest::dirtyIncludeKeepsOwnedReveal()
{
    QTemporaryDir temp; const auto root = TexpressoSession::identity(temp.filePath("main.tex"));
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Source.\n"); file.close();
    const auto child = TexpressoSession::identity(temp.filePath("child.tex"));
    const auto next = TexpressoSession::identity(temp.filePath("next.tex"));
    const auto last = TexpressoSession::identity(temp.filePath("last.tex"));
    TexpressoSession session(root, {}, {{root, "Source.\n"}, {child, "Dirty include.\n"}, {next, "Next.\n"}, {last, "Last.\n"}});
    makeReady(session); session.process = std::make_unique<TexpressoProcess>();
    session.preview.setCapabilities({{"requestLifecycle", true}});
    session.followCursor = false; // Explicit Sync still owns its continuation.
    QSignalSpy commands(&session.preview, &TexpressoPreview::command);
    session.syncForward(root, 0, true); QCOMPARE(forwardCount(commands), 1);
    session.message({"input-file", 1, child}); QVERIFY(session.sourceRefreshPending);
    const auto sequence = session.preview.sequence();
    QCOMPARE(forwardCount(commands), 1); // Wait for the replacement barrier.
    session.message({"preview-state", 9, double(sequence - 1), 1, false, 0});
    QCOMPARE(forwardCount(commands), 1);
    session.message({"preview-state", 10, double(sequence), 1, false, 0});
    QCOMPARE(forwardCount(commands), 2); QVERIFY(!session.sourceRefreshPending);
    QCOMPARE(lastForward(commands)[4].toDouble(), double(sequence));
    session.message({"preview-state", 10, double(sequence), 1, false, 0});
    QCOMPARE(forwardCount(commands), 2);
    session.message({"input-file", 2, next}); QVERIFY(session.sourceRefreshPending);
    session.preview.navigateTo(0); QVERIFY(!session.sourceRefreshPending);
    session.message({"preview-state", 11, double(session.preview.sequence()), 1, false, 0});
    QCOMPARE(forwardCount(commands), 2); // A manual hold wins over the old Sync.
    session.syncForward(root, 0, true); QCOMPARE(forwardCount(commands), 3);
    session.message({"input-file", 3, last}); QVERIFY(session.sourceRefreshPending);
    session.setPaused(true);
    session.message({"preview-state", 12, double(session.preview.sequence()), 1, false, 0});
    QCOMPARE(forwardCount(commands), 3); QVERIFY(session.sourceRefreshPending);
    session.setPaused(false); QCOMPARE(forwardCount(commands), 4); QVERIFY(!session.sourceRefreshPending);
    // A real edit replaces the earlier explicit navigation, even with follow off.
    session.sendSource({"rescan"}, true); QVERIFY(session.sourceRefreshPending);
    session.changeBuffer(root, 0, 0, 0, 0, "New ", "New Source.\n");
    QVERIFY(!session.sourceRefreshPending);
    session.message({"preview-state", 13, double(session.preview.sequence()), 1, false, 0});
    QCOMPARE(forwardCount(commands), 4);
}
QTEST_MAIN(TexpressoSourceFollowTest)
#include "source_follow_test.moc"
