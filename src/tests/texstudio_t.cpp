#include "texstudio_t.h"

#include "testutil.h"
#include "texstudio.h"
#include "editors.h"
#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QTemporaryDir>
#ifdef TEXSTUDIO_TEXPRESSO
#include "texpresso/texpressomanager.h"
#include "texpresso/texpressowidget.h"
#endif

extern Texstudio *txsInstance;

void TexStudioTest::checkIncludes_data(){
    if (!allTests){
        qDebug("skipped includes tests");
        return;
    }

    QTest::addColumn<QStringList>("files");
    QTest::addColumn<bool>("refPresent");

    QTest::newRow("simple")
        <<QStringList{QString(TESTDATADIR)+"/simple_document.tex"}<<false;
    QTest::newRow("top_with_two_level_include")
        <<QStringList{QString(TESTDATADIR)+"/top_with_two_level_include.tex"}<<true;
    QTest::newRow("top_with_two_level_include_as_second_file")
        <<QStringList{QString(TESTDATADIR)+"/included_level1.tex",QString(TESTDATADIR)+"/top_with_two_level_include.tex"}<<true;
    QTest::newRow("top_with_two_level_include_as_first_file")
        <<QStringList{QString(TESTDATADIR)+"/top_with_two_level_include.tex",QString(TESTDATADIR)+"/included_level1.tex"}<<true;
    QTest::newRow("top_with_subfile")
        <<QStringList{QString(TESTDATADIR)+"/top_with_subfile.tex"}<<false;
    QTest::newRow("subfile")
        <<QStringList{QString(TESTDATADIR)+"/subfile_level1.tex"}<<false;
    QTest::newRow("eqref")
        <<QStringList{QString(TESTDATADIR)+"/test_eqref.tex"}<<true;
    QTest::newRow("eqref via include")
        <<QStringList{QString(TESTDATADIR)+"/test_eqref_top.tex"}<<true;
    QTest::newRow("2 level include")
        <<QStringList{QString(TESTDATADIR)+"/top.tex",QString(TESTDATADIR)+"/a1.tex",QString(TESTDATADIR)+"/a2.tex"}<<false;
    QTest::newRow("2 level include, out of order")
        <<QStringList{QString(TESTDATADIR)+"/top.tex",QString(TESTDATADIR)+"/a2.tex",QString(TESTDATADIR)+"/a1.tex"}<<false;
    QTest::newRow("2 level include, buried package")
        <<QStringList{QString(TESTDATADIR)+"/included_level1a.tex",QString(TESTDATADIR)+"/included_level2a.tex",QString(TESTDATADIR)+"/top_buried_package.tex"}<<true;
    QTest::newRow("usercommands")
        <<QStringList{QString(TESTDATADIR)+"/usercommands.tex"}<<false;
}

void TexStudioTest::checkIncludes(){
    if (!allTests){
        qDebug("skipped includes tests");
        return;
    }
    QFETCH(QStringList, files);
    QFETCH(bool, refPresent);

    Texstudio *txs=txsInstance;

    // deactivate caching
    auto *conf=dynamic_cast<ConfigManager *>(ConfigManagerInterface::getInstance());
    bool cacheDocs=conf->cacheDocuments;
    conf->cacheDocuments=false;

    QList<LatexEditorView *> edViews;
    for(const QString &fn:files){
        LatexEditorView *edView=txs->load(fn);
        if(!edView){
            qDebug()<<"test file not found ! Skip !";
            return;
        }
        edViews<<edView;
    }
    for(const LatexEditorView* edView:edViews){
        LatexDocument *doc=edView->getDocument();
        doc->synChecker.waitForQueueProcess(); // wait for syntax checker to finish (as it runs in a parallel thread)
    }

    LatexDocument *doc=edViews.last()->getDocument();
    bool synError=false;
    bool refFound=false;
    for(int i=0;i<doc->lineCount();i++){
        QDocumentLineHandle *dlh=doc->line(i).handle();
        QList<QFormatRange> formats=dlh->getOverlays(LatexEditorView::syntaxErrorFormat);
        if(!formats.isEmpty())
            synError=true;
        formats=dlh->getOverlays(LatexEditorView::referencePresentFormat);
        if(!formats.isEmpty())
            refFound=true;
    }
    // cache information
    conf->cacheDocuments=true;

    for(int i=0;i<files.size();++i){
        txs->fileClose();
    }
    // restore previous cache setting
    conf->cacheDocuments=cacheDocs;

    QEQUAL(synError,false);
    QEQUAL(refFound,refPresent);
}

void TexStudioTest::checkIncludesCached_data(){
    if (!allTests){
        qDebug("skipped includes tests");
        return;
    }

    QTest::addColumn<QStringList>("files");
    QTest::addColumn<bool>("refPresent");

    QTest::newRow("simple")
        <<QStringList{QString(TESTDATADIR)+"/simple_document.tex"}<<false;
    QTest::newRow("top_with_two_level_include")
        <<QStringList{QString(TESTDATADIR)+"/top_with_two_level_include.tex"}<<true;
    QTest::newRow("top_with_two_level_include_as_second_file")
        <<QStringList{QString(TESTDATADIR)+"/included_level1.tex",QString(TESTDATADIR)+"/top_with_two_level_include.tex"}<<true;
    QTest::newRow("top_with_two_level_include_as_first_file")
        <<QStringList{QString(TESTDATADIR)+"/top_with_two_level_include.tex",QString(TESTDATADIR)+"/included_level1.tex"}<<true;
    QTest::newRow("top_with_subfile")
        <<QStringList{QString(TESTDATADIR)+"/top_with_subfile.tex"}<<false;
    QTest::newRow("subfile")
        <<QStringList{QString(TESTDATADIR)+"/subfile_level1.tex"}<<false;
    QTest::newRow("eqref")
        <<QStringList{QString(TESTDATADIR)+"/test_eqref.tex"}<<true;
    QTest::newRow("eqref via include")
        <<QStringList{QString(TESTDATADIR)+"/test_eqref_top.tex"}<<true;
    QTest::newRow("2 level include")
        <<QStringList{QString(TESTDATADIR)+"/top.tex",QString(TESTDATADIR)+"/a1.tex",QString(TESTDATADIR)+"/a2.tex"}<<false;
    QTest::newRow("2 level include, out of order")
        <<QStringList{QString(TESTDATADIR)+"/top.tex",QString(TESTDATADIR)+"/a2.tex",QString(TESTDATADIR)+"/a1.tex"}<<false;
    QTest::newRow("2 level include, buried package")
        <<QStringList{QString(TESTDATADIR)+"/included_level1a.tex",QString(TESTDATADIR)+"/included_level2a.tex",QString(TESTDATADIR)+"/top_buried_package.tex"}<<true;
}

void TexStudioTest::checkIncludesCached(){
    if (!allTests){
        qDebug("skipped includes tests");
        return;
    }
    QFETCH(QStringList, files);
    QFETCH(bool, refPresent);

    Texstudio *txs=txsInstance;
    // used cached docs
    auto *conf=dynamic_cast<ConfigManager *>(ConfigManagerInterface::getInstance());
    bool cacheDocs=conf->cacheDocuments;
    conf->cacheDocuments=true;

    LatexEditorView *edView=nullptr;
    for(const QString &fn:files){
        edView=txs->load(fn);
    }
    if(!edView){
        qDebug()<<"test file not found ! Skip !";
        return;
    }
    LatexDocument *doc=edView->getDocument();
    doc->synChecker.waitForQueueProcess(); // wait for syntax checker to finish (as it runs in a parallel thread)

    bool synError=false;
    bool refFound=false;
    for(int i=0;i<doc->lineCount();i++){
        QDocumentLineHandle *dlh=doc->line(i).handle();
        QList<QFormatRange> formats=dlh->getOverlays(LatexEditorView::syntaxErrorFormat);
        if(!formats.isEmpty())
            synError=true;
        formats=dlh->getOverlays(LatexEditorView::referencePresentFormat);
        if(!formats.isEmpty())
            refFound=true;
    }
    for(int i=0;i<files.size();++i){
        txs->fileClose();
    }
    // restore previous cache setting
    conf->cacheDocuments=cacheDocs;

    QEQUAL(synError,false);
    QEQUAL(refFound,refPresent);
}

void TexStudioTest::normalCompletion_data(){
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("line");
    QTest::addColumn<int>("column");
    QTest::addColumn<LatexCompleter::CompletionFlags>("flags");

    QTest::newRow("command") << QStringLiteral("\\be") << 0 << 3 << LatexCompleter::CompletionFlags();
    QTest::newRow("environment") << QStringLiteral("\\begin{doc") << 0 << 11 << LatexCompleter::CompletionFlags();
    QTest::newRow("ref") << QStringLiteral("\\label{test}\\ref{t") << 0 << 18 << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_REF);
    QTest::newRow("cite") << QStringLiteral("\\bibitem{test}\\cite{t") << 0 << int(QStringLiteral("\\bibitem{test}\\cite{t").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_CITE);
    //QTest::newRow("package") << QStringLiteral("\\usepackage{a") << 0 << int(QStringLiteral("\\usepackage{a").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_PACKAGE);
    //QTest::newRow("graphic") << QStringLiteral("\\usepackage{graphicx}\\includegraphics{f") << 0 << int(QStringLiteral("\\usepackage{graphicx}\\includegraphics{f").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_GRAPHIC);
    QTest::newRow("keyval_key") << QStringLiteral("\\usepackage{fancyvrb}\\fvset{f") << 0 << int(QStringLiteral("\\usepackage{fancyvrb}\\fvset{f").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_KEYVAL);
    QTest::newRow("keyval_valAfterEqual") << QStringLiteral("\\usepackage{fancyvrb}\\fvset{frame=") << 0 << int(QStringLiteral("\\usepackage{fancyvrb}\\fvset{frame=").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_KEYVAL);
    QTest::newRow("keyval_val") << QStringLiteral("\\usepackage{fancyvrb}\\fvset{frame=s") << 0 << int(QStringLiteral("\\usepackage{fancyvrb}\\fvset{frame=s").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_KEYVAL);
    QTest::newRow("keyval_keyAfterComma") << QStringLiteral("\\usepackage{fancyvrb}\\fvset{frame=s,") << 0 << int(QStringLiteral("\\usepackage{fancyvrb}\\fvset{frame=s,").size()) << LatexCompleter::CompletionFlags(LatexCompleter::CF_FORCE_KEYVAL);
}

void TexStudioTest::normalCompletion(){
    QFETCH(QString, text);
    QFETCH(int, line);
    QFETCH(int, column);
    QFETCH(LatexCompleter::CompletionFlags, flags);

    Texstudio *txs = txsInstance;
    QVERIFY2(txs, "The Texstudio instance must exist for completion tests");

    LatexEditorView *edView = txs->currentEditorView();
    if (!edView) {
        QVERIFY2(QMetaObject::invokeMethod(txs, "fileNewInternal", Qt::DirectConnection), "A new document should be created for completion tests");
        edView = txs->currentEditorView();
    }
    QVERIFY2(edView, "A LatexEditorView must be available for completion tests");
    QVERIFY2(edView->editor, "The editor should be created before invoking completion");

    edView->editor->setText(text, false);
    edView->editor->setCursor(edView->editor->document()->cursor(line, column));
    LatexEditorView::getCompleter()->close();
    QCoreApplication::processEvents();

    QVERIFY2(!LatexEditorView::getCompleter()->isVisible(), "The completer should start closed before invoking normal completion");
    QVERIFY2(QMetaObject::invokeMethod(txs, "normalCompletion", Qt::DirectConnection), "normalCompletion should be invokable");
    QCoreApplication::processEvents();
    QVERIFY2(LatexEditorView::getCompleter()->isVisible(), "normalCompletion should open the completion popup");
    QVERIFY2(LatexEditorView::getCompleter()->countWords() > 0, "normalCompletion should produce completion entries");
    QVERIFY2(gatherCompletionFlags() == flags, "normalCompletion should set the correct completion flags");
    LatexEditorView::getCompleter()->close();
}
/*!
 * \brief recreate the completion flags from the current configuration
 * Converts the current configuration into a set of completion flags that can be used to control the behavior of the completion engine.
 * \return
 */
LatexCompleter::CompletionFlags TexStudioTest::gatherCompletionFlags()
{
    LatexCompleter *completer = LatexEditorView::getCompleter();
    LatexCompleter::CompletionFlags flags;
    if(completer){
        if(completer->forcedCite)
            flags |= LatexCompleter::CF_FORCE_CITE;
        if(completer->forcedRef)
            flags |= LatexCompleter::CF_FORCE_REF;
        if(completer->forcedPackage)
            flags |= LatexCompleter::CF_FORCE_PACKAGE;
        if(completer->forcedGraphic)
            flags |= LatexCompleter::CF_FORCE_GRAPHIC;
        if(completer->forcedKeyval)
            flags |= LatexCompleter::CF_FORCE_KEYVAL;
        if(completer->forcedSpecialOption)
            flags |= LatexCompleter::CF_FORCE_SPECIALOPTION;
        if(completer->forcedLength)
            flags |= LatexCompleter::CF_FORCE_LENGTH;
    }
    return flags;
}

/*!
 * \brief check that dropped files are recognized as coming from the internal
 * file explorer only when the drag source is that widget (or its viewport),
 * see #4608 and #4644
 */
void TexStudioTest::dragDropTexFileSource()
{
    Texstudio *txs = txsInstance;
    QVERIFY2(txs, "The Texstudio instance must exist for drag/drop tests");
    QVERIFY2(txs->fileView, "The internal file explorer must exist for drag/drop tests");

    QVERIFY2(txs->isInternalFileExplorerDragSource(txs->fileView), "drag from the file explorer widget itself should be recognized as internal");
    QVERIFY2(txs->isInternalFileExplorerDragSource(txs->fileView->viewport()), "drag from the file explorer's viewport should be recognized as internal");
    QVERIFY2(!txs->isInternalFileExplorerDragSource(nullptr), "a drop without a source (e.g. from an external application) must not be recognized as internal");
    QVERIFY2(!txs->isInternalFileExplorerDragSource(txs), "a drag from an unrelated widget must not be recognized as internal");
}

void TexStudioTest::bibliographyRerunPriority()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("main.tex");
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("\\documentclass{article}\n\\begin{document}Test\\end{document}\n");
    source.close();
    auto txs = txsInstance;
    auto view = txs->load(path);
    QVERIFY(view);
    QVERIFY(txs->checkProgramPermission("txs:///biber", "bibliography", view->document));
    QVERIFY(!txs->checkProgramPermission("txs:///biber|unexpected-command", "bibliography", nullptr));
    QVERIFY(!txs->checkProgramPermission("txs:///biber --unexpected-option", "bibliography", nullptr));
    const bool oldAutomaticBibliography = txs->configManager.runLaTeXBibTeXLaTeX;
    const auto cleanup = qScopeGuard([&] {
        txs->configManager.runLaTeXBibTeXLaTeX = oldAutomaticBibliography;
        txs->fileClose();
    });
    QFile log(directory.filePath("main.log"));
    QVERIFY(log.open(QIODevice::WriteOnly));
    log.write("This is pdfTeX\n(./main.tex\n"
              "LaTeX Warning: Label(s) may have changed. Rerun to get cross-references right.\n\n"
              "Package biblatex Warning: Please (re)run Biber on the file:\n"
              "(biblatex)                main\n"
              "(biblatex)                and rerun LaTeX afterwards.\n\n)\n");
    log.close();
    txs->configManager.runLaTeXBibTeXLaTeX = true;
    LatexCompileResult result = LCR_NORMAL;
    txs->viewLogOrReRun(&result);
    QCOMPARE(result, LCR_RERUN_WITH_BIBLIOGRAPHY);
    txs->configManager.runLaTeXBibTeXLaTeX = false;
    txs->viewLogOrReRun(&result);
    QCOMPARE(result, LCR_RERUN);
}

void TexStudioTest::liveBuildAndRefresh()
{
#ifndef TEXSTUDIO_TEXPRESSO
    QSKIP("Live preview is not built");
#else
    // Opt in: this exercises the actual installed TeX distribution and Biber.
    // It never writes to an existing project or uses its auxiliary files.
    if (!qEnvironmentVariableIsSet("TEXPRESSO_TEST_BIBLIOGRAPHY"))
        QSKIP("Set TEXPRESSO_TEST_BIBLIOGRAPHY=1 to test the real Compile/Biber/live-preview workflow");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile bibliography(directory.filePath("refs.bib"));
    QVERIFY(bibliography.open(QIODevice::WriteOnly));
    bibliography.write("@book{sample,author={Ada Example},title={Bibliography resolved},year={2020}}\n");
    bibliography.close();
    const auto path = directory.filePath("main.tex");
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("% !TeX TXS-program:bibliography = txs:///biber\n"
                 "\\documentclass{article}\n\\usepackage[backend=biber]{biblatex}\n"
                 "\\addbibresource{refs.bib}\n\\begin{document}\n"
                 "Old text. Citation: \\cite{sample}.\\printbibliography\n\\end{document}\n");
    source.close();
    QFile otherSource(directory.filePath("other.tex"));
    QVERIFY(otherSource.open(QIODevice::WriteOnly));
    otherSource.write("% !TeX TXS-program:bibliography = txs:///bibtex\n"
                      "\\documentclass{article}\n\\begin{document}Other project\\end{document}\n");
    otherSource.close();
    auto txs = txsInstance;
    auto &manager = txs->buildManager;
    const auto oldCommands = manager.getAllCommands();
    const auto oldOrder = manager.getCommandsOrder();
    const auto oldSave = manager.saveFilesBeforeCompiling;
    const bool oldAutomaticBibliography = txs->configManager.runLaTeXBibTeXLaTeX;
    const bool oldEnabled = txs->texpressoAction->isChecked();
    LatexEditorView *view = nullptr, *otherView = nullptr;
    bool buildStarted = false;
    const auto cleanup = qScopeGuard([&] {
        if (buildStarted && manager.busyRunningCommands()) manager.killCurrentProcess();
        manager.setAllCommands(oldCommands, oldOrder);
        manager.saveFilesBeforeCompiling = oldSave;
        txs->configManager.runLaTeXBibTeXLaTeX = oldAutomaticBibliography;
        txs->texpressoAction->setChecked(oldEnabled);
        if (otherView) {
            txs->editors->setCurrentEditor(otherView);
            txs->fileClose();
        }
        if (view) {
            txs->editors->setCurrentEditor(view);
            view->document->setClean();
            txs->fileClose();
        }
    });
    QVERIFY(!manager.busyRunningCommands());
    auto commands = oldCommands;
    commands["compile"].commandLine = "txs:///pdflatex";
    commands["bibliography"].commandLine = "txs:///bibtex"; // Document overrides this to Biber.
    manager.setAllCommands(commands, oldOrder);
    manager.saveFilesBeforeCompiling = BuildManager::SFBC_ONLY_NAMED;
    txs->configManager.runLaTeXBibTeXLaTeX = true;
    view = txs->load(path);
    QVERIFY(view);
    txs->texpressoAction->setChecked(true);
    txs->updateTexpresso();
    auto session = txs->texpressoManager->current();
    QVERIFY(session);
    QTRY_VERIFY_WITH_TIMEOUT(!session->preview.image().isNull(), 30000);
    const auto previousPid = session->processId();
    QVERIFY(previousPid > 0);
    QVERIFY(!QFileInfo::exists(directory.filePath("main.bbl")));
    QVERIFY(!QFileInfo::exists(directory.filePath("main.bcf")));
    // External compilation must consume the same current text as live preview.
    view->editor->setText(view->editor->text().replace("Old text", "Saved text"), true);
    QVERIFY(view->editor->isContentModified());
    QSignalSpy finished(&manager, &BuildManager::buildSequenceFinished);
    bool ranBiber = false;
    bool switchedProject = false;
    QObject commandRecorder;
    connect(&manager, &BuildManager::beginRunningSubCommand, &commandRecorder,
            [&](ProcessX *, const QString &, const QString &command, const RunCommandFlags &flags) {
        ranBiber |= command == "biber";
        if (!switchedProject && flags.testFlag(RCF_COMPILES_TEX)) {
            switchedProject = true;
            QTimer::singleShot(0, &commandRecorder, [&] {
                otherView = txs->load(directory.filePath("other.tex"));
                txs->updateTexpresso();
            });
        }
    });
    buildStarted = true;
    QVERIFY(QMetaObject::invokeMethod(txs->texpressoWidget, "buildRequested", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() && !manager.busyRunningCommands(), 60000);
    bool successful = false;
    for (const auto &args : finished)
        if (TexpressoSession::identity(args[0].toString()) == session->root()) successful = args[1].toBool();
    QVERIFY(successful);
    QVERIFY(otherView);
    QCOMPARE(txs->texpressoManager->current()->root(), TexpressoSession::identity(otherSource.fileName()));
    QVERIFY2(ranBiber, "Normal Compile must honor the document's Biber override");
    QFile bbl(directory.filePath("main.bbl"));
    QVERIFY(bbl.open(QIODevice::ReadOnly));
    QVERIFY(bbl.readAll().contains("Bibliography resolved"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QVERIFY(source.readAll().contains("Saved text"));
    txs->editors->setCurrentEditor(view);
    txs->updateTexpresso();
    QTRY_VERIFY_WITH_TIMEOUT(session->processId() > 0 && session->processId() != previousPid, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid(), 30000);
    QVERIFY2(session->compilerWarning.isEmpty(), qPrintable(session->compilerWarning));
    QVERIFY2(session->compilerError.isEmpty(), qPrintable(session->compilerError));
#endif
}
