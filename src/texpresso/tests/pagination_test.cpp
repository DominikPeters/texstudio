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

class PaginationTest : public QObject
{
    Q_OBJECT
private slots:
    void editForgetsExactCount();
    void previousTotalRemainsBestEstimate();
    void completionRetiresBeyondEndRequests();
    void failedFrontierStopsAndRepairs();
    void failedBeforeFirstPage();
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
    void providedPaperLastPage();
};
static QJsonArray last(const QSignalSpy &commands) { return commands.last()[0].toJsonArray(); }
static void deliver(TexpressoPreview &preview, const QJsonArray &request)
{
    QImage image(16, 24, QImage::Format_RGB32); image.fill(Qt::white);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    preview.receive({{"requestId", request[1]}, {"revision", request[4]}, {"sequence", request[5]},
        {"page", request[2]}, {"width", 16}, {"height", 24}, {"bounds", QJsonArray{0, 0, 600, 800}},
        {"mime", "image/png"}, {"byteLength", bytes.size()}}, bytes);
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
    manager.reconcileBuffer(root, "\\documentclass{article}\n\\begin{document}\nOne.\\newpage\n\\undefinedPaginationCommand\n\\end{document}\n");
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
    }
    const int page = live->preview.page();
    QTRY_VERIFY(page < widget.pageRects.size());
    const int target = std::clamp(widget.pageRects[page].top() - 12, 0, widget.area->verticalScrollBar()->maximum());
    QTRY_VERIFY2_WITH_TIMEOUT(std::abs(widget.area->verticalScrollBar()->value() - target) <= 2,
        qPrintable(QString("viewport=%1 expected=%2\n%3").arg(widget.area->verticalScrollBar()->value()).arg(target).arg(diagnostic())), 5000);
    qInfo().noquote() << diagnostic(); manager.shutdown();
}
QTEST_MAIN(PaginationTest)
#include "pagination_test.moc"
