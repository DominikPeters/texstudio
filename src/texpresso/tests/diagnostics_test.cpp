#include <QtTest>
#include "texpressowidget.h"
#include <QAction>
#include <QFile>
#include <QTemporaryDir>

class TexpressoDiagnosticsTest : public QObject
{
    Q_OBJECT
private slots:
    void compilerErrorsAndRecovery();
    void bibliographyWarnings();
    void batchedLogUpdates();
    void diagnosticSourceGeneration();
    void liveOutputAndStatus();
};

void TexpressoDiagnosticsTest::compilerErrorsAndRecovery()
{
    TexpressoSession session("/tmp/main.tex", QString(), {});
    QSignalSpy updates(&session, &TexpressoSession::updated);
    session.message({"append-lines", "out", "LaTeX Warning: Reference `later' undefined.",
        "Overfull \\hbox (4.0pt too wide)"});
    session.message({"flush"});
    QVERIFY(session.compilerError.isEmpty()); QVERIFY(session.compilerWarning.isEmpty());
    session.message({"append-lines", "out", "! Undefined control sequence.", "l.17 \\unknowncommand", "! Emergency stop."});
    session.message({"flush"});
    QVERIFY(session.compilerError.startsWith("Undefined control sequence."));
    QVERIFY(session.compilerError.contains("l.17 \\unknowncommand"));
    QVERIFY(!session.failed()); QVERIFY(!updates.isEmpty());
    // Truncation is the engine's checkpoint rollback, not a new OS process.
    session.message({"truncate-lines", "out", 2});
    session.message({"flush"});
    QVERIFY(session.compilerError.isEmpty());
    QVERIFY(!session.compilerOutput.contains("Undefined control sequence"));
    session.message({"append-lines", "log", "! LaTeX Error: File `missing.sty' not found."});
    session.message({"flush"});
    QVERIFY(session.compilerError.contains("missing.sty"));
    session.message({"truncate-lines", "log", 0});
    session.message({"flush"});
    QVERIFY(session.compilerError.isEmpty());
}

void TexpressoDiagnosticsTest::bibliographyWarnings()
{
    TexpressoSession session("/tmp/main.tex", QString(), {});
    session.message({"append-lines", "out", "No file main.bbl."});
    session.message({"flush"});
    QVERIFY(session.compilerWarning.contains("Build & Refresh"));
    session.message({"append-lines", "log", "Package biblatex Warning: Please (re)run Biber on the file:", "(biblatex) main"});
    session.message({"flush"});
    QVERIFY(session.compilerWarning.contains("needs Biber"));
    session.message({"truncate-lines", "out", 0});
    session.message({"truncate-lines", "log", 0});
    session.message({"flush"});
    QVERIFY(session.compilerWarning.isEmpty());
    session.message({"append-lines", "log", "Package biblatex Warning: Please (re)run BibTeX on the file:"});
    session.message({"flush"});
    QVERIFY(session.compilerWarning.contains("needs BibTeX"));
    session.message({"truncate-lines", "log", 0});
    session.message({"append-lines", "log", "LaTeX Warning: Label(s) may have changed. Rerun to get cross-references right."});
    session.message({"flush"});
    QVERIFY(session.compilerWarning.isEmpty());
}

void TexpressoDiagnosticsTest::batchedLogUpdates()
{
    TexpressoSession session("/tmp/main.tex", QString(), {});
    session.message({"append-lines", "out", "! Undefined control sequence."});
    session.message({"flush"});
    QVERIFY(!session.compilerError.isEmpty());
    QSignalSpy publications(&session, &TexpressoSession::outputUpdated);
    // The engine can replace a checkpoint's log through thousands of small
    // messages. Publish the complete replacement once, without retaining the
    // old error or repeatedly joining an ever-growing document log.
    session.message({"truncate-lines", "out", 0});
    for (int i = 0; i < 7000; ++i)
        session.message({"append-lines", "out", QString("Package loading line %1").arg(i)});
    session.message({"append-lines", "out", "No file main.bbl."});
    QCOMPARE(publications.size(), 0);
    QTRY_COMPARE(publications.size(), 1);
    QVERIFY(session.compilerError.isEmpty());
    QVERIFY(session.compilerOutput.contains("Package loading line 6999"));
    QVERIFY(session.compilerWarning.contains("Build & Refresh"));
    // A flush publishes without waiting for another event-loop iteration.
    session.message({"append-lines", "out", "! Emergency stop."});
    session.message({"flush"});
    QCOMPARE(publications.size(), 2);
    QCOMPARE(session.compilerError, QString("Emergency stop."));
    QCoreApplication::processEvents();
    QCOMPARE(publications.size(), 2);
}

void TexpressoDiagnosticsTest::diagnosticSourceGeneration()
{
    TexpressoSession session("/tmp/main.tex", QString(), {});
    session.message({"append-lines", "log", "! Undefined control sequence.", "l.3 \\oldError"});
    session.message({"flush"});
    QCOMPARE(session.compilerLogSourceSequence(), qint64(0));
    QVERIFY(session.compilerOutputCurrent());
    session.preview.sourceChanged(1);
    QVERIFY(!session.compilerOutputCurrent());
    session.message({"preview-state", 1, 1, 0, false, false});
    // A source barrier acknowledgment or stderr publication does not prove
    // that the old TeX log belongs to the newly accepted source buffer.
    QVERIFY(!session.compilerOutputCurrent());
    emit session.outputUpdated();
    QVERIFY(!session.compilerOutputCurrent());
    session.message({"truncate-lines", "log", 0});
    session.message({"append-lines", "log", "This is XeTeX"});
    session.message({"flush"});
    QCOMPARE(session.compilerLogSourceSequence(), qint64(1));
    QVERIFY(session.compilerOutputCurrent());
    QVERIFY(session.compilerError.isEmpty());
}

void TexpressoDiagnosticsTest::liveOutputAndStatus()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for live diagnostics integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    const QString good = "\\documentclass{article}\n\\begin{document}\nWorking preview.\n\\end{document}\n";
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(good.toUtf8()); file.close();
    TexpressoManager manager(nullptr, engine);
    PDFDocumentConfig config{}; config.editTool = 1; config.magnifierSize = 120;
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(600, 600); widget.show(); manager.activate(root);
    auto session = manager.current(); QVERIFY(session);
    session->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() || session->failed(), 20000);
    QVERIFY2(!session->failed(), qPrintable(session->status));
    auto status = widget.findChild<QLabel *>("texpressoStatus"); QVERIFY(status);
    auto output = widget.findChild<QToolButton *>("texpressoOutput"); QVERIFY(output);
    QSignalSpy outputRequests(&widget, &TexpressoWidget::outputRequested);
    QTest::mouseClick(output, Qt::LeftButton);
    QCOMPARE(outputRequests.size(), 1);
    manager.openBuffer(root, "\\documentclass{article}\n\\usepackage{texpresso_missing_package}\n\\begin{document}\nBroken.\n\\end{document}\n");
    QTRY_VERIFY_WITH_TIMEOUT(session->compilerError.contains("texpresso_missing_package"), 20000);
    QTRY_VERIFY(status->text().contains("texpresso_missing_package"));
    QVERIFY(!status->text().contains("Updating preview"));
    QVERIFY(session->compilerLogOutput().contains("texpresso_missing_package"));
    QVERIFY(!session->failed());
    manager.openBuffer(root, good);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && session->compilerError.isEmpty(), 20000);
    QTRY_VERIFY(!status->text().startsWith("TeX error:"));
    QTRY_VERIFY(!session->compilerLogOutput().contains("! LaTeX Error: File `texpresso_missing_package.sty'"));
    session->message({"append-lines", "out", "No file main.bbl."});
    QTRY_VERIFY(status->text().contains("Build & Refresh"));
    QSignalSpy build(&widget, &TexpressoWidget::buildRequested);
    widget.viewerActions()["build"]->trigger(); QCOMPARE(build.size(), 1);
    QVERIFY(widget.viewerActions()["rerunOnce"]->toolTip().contains("BibTeX or Biber"));
}

QTEST_MAIN(TexpressoDiagnosticsTest)
#include "diagnostics_test.moc"
