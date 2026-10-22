#include <QtTest>
#include "texpressomanager.h"
#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>

class RerunTest : public QObject
{
    Q_OBJECT
private slots:
    void terminalWithoutPages();
    void stableRerun_data();
    void stableRerun();
    void errorRerun();
};
static QString engine() { return qEnvironmentVariable("TEXPRESSO_TEST_ENGINE"); }
static void writeFile(const QString &path, const QString &text)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text.toUtf8()), text.toUtf8().size());
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
    live->followCursor = false; live->setAutoRerun(automatic);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() || live->failed(), 20000);
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
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("auxiliary files are already stable"), 15000);
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
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() || live->failed(), 20000);
    QVERIFY2(live->preview.valid(), qPrintable(live->previewError + live->engineOutput));
    QVERIFY(live->previewError.isEmpty());
}
QTEST_MAIN(RerunTest)
#include "rerun_test.moc"
