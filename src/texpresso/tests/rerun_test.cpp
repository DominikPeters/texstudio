#include <QtTest>
#include "texpressomanager.h"
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

class RerunTest : public QObject
{
    Q_OBJECT
private slots:
    void terminalWithoutPages();
    void stableRerun_data();
    void stableRerun();
    void errorRerun();
private:
    void traceSession(TexpressoSession *live, QFile &trace, const QString &suffix);
};
static QString engine() { return qEnvironmentVariable("TEXPRESSO_TEST_ENGINE"); }
static void writeFile(const QString &path, const QString &text)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text.toUtf8()), text.toUtf8().size());
}
void RerunTest::traceSession(TexpressoSession *live, QFile &trace, const QString &suffix)
{
    const auto path = qEnvironmentVariable("TEXPRESSO_RERUN_TRACE");
    if (path.isEmpty()) return;
    trace.setFileName(path + suffix); QVERIFY(trace.open(QIODevice::WriteOnly));
    connect(live->process.get(), &TexpressoProcess::diagnostic, &trace, [&trace](const QString &text) { trace.write(text.toUtf8()); trace.flush(); });
    connect(live->process.get(), &TexpressoProcess::message, &trace, [&trace](const QJsonArray &message) {
        trace.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'); trace.flush();
    });
}
void RerunTest::terminalWithoutPages()
{
    TexpressoPreview preview;
    QSignalSpy commands(&preview, &TexpressoPreview::command), failures(&preview, &TexpressoPreview::failed);
    preview.setActive(true); preview.sourceChanged(1);
    preview.message({"preview-state", 2, 1, 0, true, 0});
    QCOMPARE(failures.size(), 1); QVERIFY(!preview.valid()); QVERIFY(commands.isEmpty());
    // Old states cannot report a failure against a newer source generation.
    preview.sourceChanged(2); preview.message({"preview-state", 2, 1, 0, true, 0});
    QCOMPARE(failures.size(), 1);
    // A subsequent successful compile must still render normally.
    preview.message({"preview-state", 3, 2, 1, true, 0});
    QCOMPARE(commands.size(), 1);
    const auto request = commands.last()[0].toJsonArray();
    QImage pixels(2, 2, QImage::Format_RGB32); pixels.fill(Qt::red);
    QByteArray bytes; QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(pixels.save(&buffer, "PNG"));
    preview.receive({{"requestId", request[1]}, {"revision", 3}, {"sequence", 2}, {"page", 0},
        {"bounds", QJsonArray{0, 0, 612, 792}}, {"width", 2}, {"height", 2},
        {"mime", "image/png"}, {"byteLength", bytes.size()}}, bytes);
    QTRY_VERIFY(preview.valid()); QCOMPARE(preview.image(), pixels);
    commands.clear(); preview.sourceChanged(3);
    preview.message({"preview-state", 4, 3, 0, true, 0});
    QCOMPARE(failures.size(), 2); QVERIFY(!preview.valid()); QVERIFY(commands.isEmpty());
    QCOMPARE(preview.image(), pixels); // Retain the previous picture as stale.
}
void RerunTest::stableRerun_data()
{
    QTest::addColumn<bool>("automatic");
    QTest::newRow("automatic") << true;
    QTest::newRow("manual") << false;
}
void RerunTest::stableRerun()
{
    if (engine().isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler integration tests");
    QFETCH(bool, automatic);
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex");
    writeFile(root, "\\documentclass{article}\n\\begin{document}\nStable.\\end{document}\n");
    TexpressoManager manager(nullptr, engine()); manager.activate(root); auto live = manager.current();
    QFile trace; traceSession(live, trace, automatic ? ".automatic" : ".manual");
    live->followCursor = false; live->setAutoRerun(automatic);
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.valid() || live->failed(), qPrintable(live->status + '\n' + live->engineOutput), 20000);
    QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
    if (automatic) QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("aux byte-stable, convergence reached"), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    QSignalSpy failures(&live->preview, &TexpressoPreview::failed);
    for (int pass = 0; pass < 3; ++pass) {
        const auto sequence = live->preview.sequence(); live->rerunOnce();
        QVERIFY(live->preview.sequence() > sequence);
        QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() || live->failed(), 15000);
        QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
        QVERIFY2(live->previewError.isEmpty(), qPrintable(live->previewError));
        QCOMPARE(failures.size(), 0);
    }
    QTRY_VERIFY2_WITH_TIMEOUT(live->engineOutput.contains("auxiliary files are already stable"), qPrintable(live->engineOutput.right(10000)), 15000);
    // A no-op click must be consumed now, not become a surprise finishing pass
    // when the user next edits while automatic reruns are disabled.
    live->setAutoRerun(false);
    live->engineOutput.clear(); // The bounded diagnostic tail may evict old passes.
    manager.openBuffer(root, "\\documentclass{article}\n\\begin{document}\nEdited.\\end{document}\n");
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    QTest::qWait(750);
    QVERIFY(!live->engineOutput.contains("on-demand: finishing pass"));
}
void RerunTest::errorRerun()
{
    if (engine().isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler integration tests");
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex");
    writeFile(root, "\\documentclass{article}\n\\begin{document}\n\\undefinedcommand\n\\end{document}\n");
    TexpressoManager manager(nullptr, engine()); manager.activate(root); auto live = manager.current();
    QFile trace; traceSession(live, trace, ".error");
    live->followCursor = false; live->setAutoRerun(false);
    QTRY_VERIFY_WITH_TIMEOUT(!live->previewError.isEmpty() || live->failed(), 20000);
    QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
    for (int pass = 0; pass < 2; ++pass) {
        live->rerunOnce();
        QTRY_VERIFY_WITH_TIMEOUT(!live->previewError.isEmpty() || live->failed(), 15000);
        QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
        QVERIFY(!live->preview.valid());
    }
    manager.openBuffer(root, "\\documentclass{article}\n\\begin{document}\nRecovered.\\end{document}\n");
    QTRY_VERIFY2_WITH_TIMEOUT(live->preview.valid() || live->failed(), qPrintable(QString("sequence=%1 revision=%2 page=%3 count=%4 complete=%5 busy=%6 inFlight=%7 dirty=%8 nativePageNeeded=%9 status=%10 error=%11\n%12")
        .arg(live->preview.sequence()).arg(live->preview.revision()).arg(live->preview.page())
        .arg(live->preview.pageCount()).arg(live->preview.complete()).arg(live->preview.busy())
        .arg(live->preview.inFlight).arg(live->preview.dirty).arg(live->preview.needsNativePage)
        .arg(live->status, live->previewError, live->engineOutput)), 20000);
    QVERIFY2(live->preview.valid(), qPrintable(live->previewError + live->engineOutput));
    QVERIFY(live->previewError.isEmpty());
}
QTEST_MAIN(RerunTest)
#include "rerun_test.moc"
