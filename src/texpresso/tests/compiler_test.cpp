#include <QtTest>
#include "texpressomanager.h"
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

class CompilerTest : public QObject
{
    Q_OBJECT
private slots:
    void convergence();
    void bibliography_data();
    void bibliography();
    void buildState();
    void projectBuildLifecycle();
};
static void writeFile(const QString &path, const QString &text)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text.toUtf8()), text.toUtf8().size());
}
static QString engine() { return qEnvironmentVariable("TEXPRESSO_TEST_ENGINE"); }
static QString executable(const QString &name)
{
    return QStandardPaths::findExecutable(name, TexpressoProcess::texliveEnvironment().value("PATH").split(QDir::listSeparator()));
}
static bool runTool(const QString &tool, const QStringList &arguments, const QString &directory, QByteArray *output)
{
    QProcess process; process.setProcessEnvironment(TexpressoProcess::texliveEnvironment());
    process.setWorkingDirectory(directory); process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable(tool), arguments);
    const bool done = process.waitForFinished(45000);
    *output = process.readAll();
    return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
void CompilerTest::convergence()
{
    if (engine().isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler integration tests");
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex");
    const QString source = "\\documentclass{article}\n\\usepackage{refcount}\n\\begin{document}\n"
        "\\typeout{FORWARD-REF:\\getrefnumber{later}}\n\\tableofcontents\nFirst \\ref{later}.\n"
        "\\newpage\n\\section{Later}\\label{later}\n\\addtocontents{toc}{\\protect\\typeout{TOC-CONSUMED}}\nSecond.\n\\end{document}\n";
    writeFile(root, source); TexpressoManager manager(nullptr, engine()); manager.activate(root);
    auto live = manager.current(); QVERIFY(live); QVERIFY(live->autoRerun()); live->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("aux byte-stable, convergence reached") || live->failed(), 30000);
    QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
    QVERIFY2(live->compilerOutput.contains("FORWARD-REF:1"), qPrintable(live->compilerOutput));
    QVERIFY2(live->compilerOutput.contains("TOC-CONSUMED"), qPrintable(live->compilerOutput));
    live->setAutoRerun(false); QVERIFY(!live->autoRerun());
    // Changing an already-converged label must receive another real finishing
    // pass on demand, rather than merely retaining a previous good picture.
    const auto changed = QString(source).replace("\\section{Later}", "\\setcounter{section}{6}\\section{Later}");
    manager.openBuffer(root, changed);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 15000);
    live->rerunOnce();
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("on-demand: finishing pass"), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("FORWARD-REF:7"), 15000);
}
void CompilerTest::bibliography_data()
{
    QTest::addColumn<bool>("biber"); QTest::addColumn<bool>("outputDirectory");
    QTest::newRow("bibtex-new-bbl") << false << false;
    QTest::newRow("bibtex-aux-directory") << false << true;
    QTest::newRow("biber-new-bbl") << true << false;
    QTest::newRow("biber-aux-directory") << true << true;
}
void CompilerTest::bibliography()
{
    QFETCH(bool, biber); QFETCH(bool, outputDirectory);
    if (engine().isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler integration tests");
    if (executable("xelatex").isEmpty() || executable(biber ? "biber" : "bibtex").isEmpty()) QSKIP("Required TeX Live tools missing");
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex");
    QVERIFY(QDir(temp.path()).mkdir("build"));
    QString source = "\\documentclass{article}\n";
    source += biber ? "\\usepackage[backend=biber]{biblatex}\n\\addbibresource{refs.bib}\n" : "";
    source += "\\begin{document}\n\\input{child}\n\\cite{sample}\n\\newpage\nSecond page.\n";
    source += biber ? "\\printbibliography\n" : "\\bibliographystyle{plain}\n\\bibliography{refs}\n";
    source += "\\end{document}\n";
    writeFile(root, source); writeFile(temp.filePath("child.tex"), "Disk child.\n");
    const auto bib = temp.filePath("refs.bib");
    const auto entry = [](const QString &value) { return "@book{sample, author={Author, Anne}, title={" + value + " \\typeout{BIB-" + value + "}}, year={2020}, publisher={Example}}\n"; };
    writeFile(bib, entry("ALPHA"));
    TexpressoManager manager(nullptr, engine()); manager.openBuffer(temp.filePath("child.tex"), "Unsaved child. \\typeout{UNSAVED-CHILD}\n"); manager.activate(root);
    auto live = manager.current(); // Default follow-cursor must not reset view after a build.
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() || live->failed(), 20000);
    QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
    QVERIFY(!live->compilerOutput.contains("BIB-ALPHA"));
    live->preview.setPage(1); QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 10000);
    live->preview.zoom = 1.5; live->preview.scroll = {21, 39}; live->setAutoRerun(false);
    const auto originalPid = live->processId(); QByteArray output;
    const QStringList dirs = outputDirectory ? QStringList{temp.filePath("build")} : QStringList{};
    const auto build = [&]() {
        QStringList args = {"-interaction=nonstopmode", "-halt-on-error"};
        if (outputDirectory) args << "-output-directory=build";
        args << "main.tex";
        if (!runTool("xelatex", args, temp.path(), &output)) return false;
        const QString job = outputDirectory ? "build/main" : "main";
        QStringList bibArgs;
        if (biber && outputDirectory) bibArgs << "--input-directory=build" << "--output-directory=build" << "main";
        else bibArgs << job;
        if (!runTool(biber ? "biber" : "bibtex", bibArgs, temp.path(), &output)) return false;
        return runTool("xelatex", args, temp.path(), &output) && runTool("xelatex", args, temp.path(), &output);
    };
    manager.beginExternalBuild(root, dirs);
    QVERIFY(live->externalBuildRunning()); QVERIFY2(build(), output.constData()); manager.endExternalBuild(root, true);
    QCOMPARE(manager.current(), live);
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("BIB-ALPHA") || live->failed(), 25000);
    QVERIFY2(!live->failed(), qPrintable(live->status + live->engineOutput));
    QVERIFY2(live->compilerOutput.contains("BIB-ALPHA"), qPrintable(live->compilerOutput));
    QVERIFY(live->processId() != originalPid); QVERIFY(!live->autoRerun());
    QCOMPARE(live->preview.page(), 1); QCOMPARE(live->preview.zoom, 1.5); QCOMPARE(live->preview.scroll, QPoint(21, 39));
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("UNSAVED-CHILD"), 10000);
    writeFile(bib, entry("BETA")); manager.beginExternalBuild(root, dirs);
    QVERIFY2(build(), output.constData()); manager.endExternalBuild(root, true);
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("BIB-BETA") || live->failed(), 25000);
    QVERIFY2(live->compilerOutput.contains("BIB-BETA"), qPrintable(live->compilerOutput + live->engineOutput));
    QVERIFY(!live->compilerOutput.contains("BIB-ALPHA"));
}
void CompilerTest::buildState()
{
    if (engine().isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler integration tests");
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex"), other = temp.filePath("other.tex");
    const QString source = "\\documentclass{article}\n\\begin{document}\n\\input{state.dat}\n\\end{document}\n";
    writeFile(root, source); writeFile(other, "\\documentclass{article}\\begin{document}Other.\\end{document}");
    writeFile(temp.filePath("state.dat"), "Old. \\typeout{STATE-OLD}\n");
    TexpressoManager manager(nullptr, engine()); manager.activate(root); auto live = manager.current();
    live->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("STATE-OLD"), 20000);
    const auto pid = live->processId(); manager.beginExternalBuild(root);
    writeFile(temp.filePath("state.dat"), "Partial. \\typeout{STATE-PARTIAL}\n");
    QTest::qWait(300); manager.endExternalBuild(root, false);
    QTest::qWait(500); QCOMPARE(live->processId(), pid);
    live->setPaused(true); manager.beginExternalBuild(root);
    manager.activate(other); manager.endExternalBuild(root, true);
    QVERIFY(live->userPaused); QVERIFY(!live->active()); QCOMPARE(manager.current()->root(), TexpressoSession::identity(other));
    manager.activate(root); QVERIFY(live->userPaused); QVERIFY(!live->preview.valid());
    live->setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("STATE-PARTIAL"), 20000);
    live->stop(); manager.beginExternalBuild(root); manager.endExternalBuild(root, true);
    QVERIFY(live->userStopped); QTRY_COMPARE(live->processId(), qint64(0));
    manager.restart(); QVERIFY(!live->userStopped);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid(), 20000);
}
void CompilerTest::projectBuildLifecycle()
{
    if (engine().isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler integration tests");
    QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("main.tex");
    QVERIFY(QDir(temp.path()).mkdir("build")); const auto auxiliary = temp.filePath("build");
    writeFile(root, "\\documentclass{article}\n\\begin{document}\n\\input{result.dat}\n\\end{document}\n");
    writeFile(temp.filePath("build/result.dat"), "Built. \\typeout{IMPORTED-RESULT}\n");
    TexpressoManager manager(nullptr, engine());
    manager.beginExternalBuild(root, {auxiliary}); manager.activate(root); auto live = manager.current();
    QVERIFY(live->externalBuildRunning()); QCOMPARE(live->processId(), qint64(0));
    // Nested subprocess/recipe notifications must not restart before the outer
    // sequence completes, including when a project was first opened mid-build.
    manager.beginExternalBuild(root, {auxiliary}); manager.endExternalBuild(root, true);
    QVERIFY(live->externalBuildRunning()); QCOMPARE(live->processId(), qint64(0));
    manager.endExternalBuild(root, true);
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("IMPORTED-RESULT"), 20000);
    QCOMPARE(manager.auxiliaryDirectories(root), QStringList{auxiliary});
    manager.retainProjects({}); QCOMPARE(manager.sessionCount(), 0);
    manager.activate(root); live = manager.current();
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("IMPORTED-RESULT"), 20000);
    // A subsequent compiler recipe using the root directory clears the former
    // output-directory search path rather than retaining stale products.
    writeFile(temp.filePath("result.dat"), "Root. \\typeout{ROOT-RESULT}\n");
    manager.beginExternalBuild(root, {});
    QVERIFY(manager.auxiliaryDirectories(root).isEmpty()); // Nested bibliography sees current recipe, not old build/.
    manager.beginExternalBuild(root, manager.auxiliaryDirectories(root));
    manager.endExternalBuild(root, true); manager.endExternalBuild(root, true);
    QVERIFY(manager.auxiliaryDirectories(root).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(live->compilerOutput.contains("ROOT-RESULT"), 20000);
    QVERIFY(!live->compilerOutput.contains("IMPORTED-RESULT"));
}
QTEST_MAIN(CompilerTest)
#include "compiler_test.moc"
