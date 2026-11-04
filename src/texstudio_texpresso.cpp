// The editor-specific adapter is kept outside the protocol/session library.
#include "texstudio.h"
#include "texpresso/texpressomanager.h"
#include "texpresso/texpressowidget.h"
#include "texpresso/texpressowslsetup.h"
#include "ViewerPanel.h"
#ifndef NO_POPPLER_PREVIEW
#include "PDFDocument.h"
#endif
#include "minisplitter.h"
#include "qdocument.h"
#include "qdocumentcursor.h"
#include "qeditor.h"
#include "utilsSystem.h"
#include <QPointer>
#include <QSettings>
#include <QScreen>
#include <QStandardPaths>
#include <QCoreApplication>

namespace {
QString bufferText(LatexDocument *document) { return document->textLines().join('\n'); }
}

void Texstudio::setupTexpresso()
{
    previewPanel = new ViewerPanel(configManager.getSettings(), mainHSplitter);
    mainHSplitter->addWidget(previewPanel); previewPanel->hide();
#ifdef NO_POPPLER_PREVIEW
    previewPanel->setPdfSupported(false);
#endif
    TexpressoProcess::setWslDistribution(configManager.getSettings()->value("texpresso/wslDistribution").toString());
    TexpressoProcess::setPackageProvider(configManager.getSettings()->value("texpresso/packageProvider", "auto").toString(),
        configManager.getSettings()->value("texpresso/miktexAutoInstall", false).toBool());
    texpressoManager = new TexpressoManager(this);
#ifdef Q_OS_WIN
    connect(&buildManager, &BuildManager::commandLineRequested, this,
            [this](const QString &id, QString *command, bool *) {
        static const QSet<QString> tools = {"latex", "pdflatex", "xelatex", "lualatex", "latexmk", "bibtex", "bibtex8", "biber", "makeindex", "texindy", "makeglossaries"};
        if (!tools.contains(id) || !configManager.getSettings()->value("texpresso/wslBuilds", (QStandardPaths::findExecutable("kpsewhich.exe").isEmpty() && QStandardPaths::findExecutable("findtexmf.exe").isEmpty())).toBool()) return;
        const auto program = CommandInfo::getProgramName(*command);
        auto name = QFileInfo(CommandInfo::getProgramNameUnquoted(*command)).fileName().toLower();
        if (name.endsWith(".exe")) name.chop(4);
        if (!tools.contains(name)) return; // Leave unrelated custom recipes intact.
        const auto runner = QCoreApplication::applicationDirPath() + "/texstudio-wsl.exe";
        const auto distribution = configManager.getSettings()->value("texpresso/wslDistribution").toString();
        *command = quoteSpaces(runner) + (distribution.isEmpty() ? QString() : " --distribution " + quoteSpaces(distribution))
            + " --provider " + quoteSpaces(configManager.getSettings()->value("texpresso/packageProvider", "auto").toString())
            + " -- " + name + command->trimmed().mid(program.size());
    });
#endif
    // BuildManager captures the root before launching the recipe, so switching
    // editor tabs while LaTeX/BibTeX/Biber run cannot refresh another project.
    connect(&buildManager, &BuildManager::buildSequenceStarted, texpressoManager,
            [this](const QString &root, QStringList directories, bool compilerSequence) {
        // A standalone bibliography tool may omit the compiler's output path.
        // A compiler recipe is authoritative, including returning to root output.
        ++previewBuildDepth[TexpressoSession::identity(root)];
        if (directories.isEmpty() && !compilerSequence) directories = texpressoManager->auxiliaryDirectories(root);
        texpressoManager->beginExternalBuild(root, directories);
    });
    connect(&buildManager, &BuildManager::buildSequenceFinished, texpressoManager,
            [this](const QString &root, bool success) {
        texpressoManager->endExternalBuild(root, success);
        const auto key = TexpressoSession::identity(root);
        if (--previewBuildDepth[key] <= 0) previewBuildDepth.remove(key);
        if (success && previewPanel->project() == key && previewPanel->mode() == ViewerPanel::Pdf) ensurePreviewPdf();
    });
    auto previewConfig = configManager.pdfDocumentConfig;
    configManager.registerOption("Preview/EditTool", &previewConfig->editTool, 1);
    configManager.registerOption("Preview/Scrolling Follows Cursor", &previewConfig->followFromCursor, false);
    configManager.registerOption("Preview/Invert Colors", &previewConfig->invertColors, false);
    configManager.registerOption("Preview/Grayscale", &previewConfig->grayscale, false);
    ViewerControls::initializeToolCursors([](const QString &name) {
        return loadPixmapFromSVG(getRealIconFile(name), QSize(32, 32));
    });
    texpressoWidget = new TexpressoWidget(texpressoManager, previewConfig, getRealIcon, previewPanel);
    connect(texpressoWidget, &TexpressoWidget::environmentSetupRequested, this, [this] {
        const auto session = texpressoManager->current();
        if (configureTexpressoEnvironment(this, configManager.getSettings(), session ? session->root() : QString())) texpressoManager->restartAll();
    });
    previewPanel->setLiveViewer(texpressoWidget);
    texpressoWidget->setPreviewVisible(false);
    connect(previewPanel, &ViewerPanel::liveVisibilityChanged, texpressoWidget, &TexpressoWidget::setPreviewVisible);
    connect(previewPanel, &ViewerPanel::pdfRequested, this, [this] { ensurePreviewPdf(); });
    connect(previewPanel, &ViewerPanel::currentViewerChanged, this, [this] {
        const bool pdf = previewPanel->mode() == ViewerPanel::Pdf && previewPanel->pdf(previewPanel->project());
        setEnabledMenusEnlargeShrink(pdf && !configManager.viewerEnlarged, pdf && configManager.viewerEnlarged);
    });
    connect(previewPanel, &ViewerPanel::buildRequested, this, [this] {
        const auto root = documents.getRootDocumentForDoc();
        if (root && TexpressoSession::identity(root->getFileName()) == previewPanel->project()) runCommand(BuildManager::CMD_COMPILE);
    });
    connect(previewPanel, &ViewerPanel::modeChanged, this, [this](ViewerPanel::Mode mode) {
        if (mode == ViewerPanel::Live && texpressoManager->current()
            && previewPanel->project() != texpressoManager->current()->root()) {
            previewPanel->setProject(texpressoManager->current()->root());
            previewPanel->setMode(ViewerPanel::Live);
            return;
        }
        if (mode == ViewerPanel::Live && configManager.viewerEnlarged) shrinkEmbeddedPDFViewer();
        const bool pdf = mode == ViewerPanel::Pdf && previewPanel->pdf(previewPanel->project());
        setEnabledMenusEnlargeShrink(pdf && !configManager.viewerEnlarged, pdf && configManager.viewerEnlarged);
    });
    setupTexpressoOutput();
    // Use the PDF action IDs so configured PDF shortcuts also reach the live
    // panel. Widget-scoped contexts prevent clashes with the editor or PDF.
    for (auto it = texpressoWidget->viewerActions().cbegin(); it != texpressoWidget->viewerActions().cend(); ++it) {
        const QString menu = it.key() == "gotoSource" ? "window" : it.key() == "followCursor" ? "config" : "view";
        it.value()->setObjectName("pdf/" + menu + '/' + it.key());
    }
    configManager.modifyManagedShortcuts("pdf");
    const int iconSize = qRound(configManager.guiPDFToolbarIconSize * QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96);
    texpressoWidget->configureAppearance(configManager.getOption("GUI/Style").toInt() != 0, iconSize);
    connect(texpressoWidget, &TexpressoWidget::followCursorChanged, this, [this](bool enabled) {
        configManager.pdfDocumentConfig->followFromCursor = enabled;
        updateTexpresso();
        if (enabled && currentEditor()) {
            const auto cursor = currentEditor()->cursor();
            if (auto document = qobject_cast<LatexDocument *>(cursor.document()))
                if (auto session = texpressoManager->current())
                    session->syncForward(document->getFileName(), cursor.lineNumber(), true, cursor.columnNumber());
        }
    });
    connect(texpressoWidget, &TexpressoWidget::toolChanged, this, [this](int tool) { configManager.pdfDocumentConfig->editTool = tool; });
    texpressoAction = getManagedMenu("main/view/show")->addAction(tr("Preview"));
    texpressoAction->setObjectName("texpressoLivePreviewAction");
    texpressoAction->setCheckable(true);
    texpressoAction->setChecked(configManager.getSettings()->value("texpresso/enabled", true).toBool());
    texpressoActivationTimer.setSingleShot(true); texpressoActivationTimer.setInterval(75);
    connect(&texpressoActivationTimer, &QTimer::timeout, this, &Texstudio::updateTexpresso);
    connect(texpressoAction, &QAction::toggled, this, [this](bool enabled) {
        configManager.getSettings()->setValue("texpresso/enabled", enabled);
        if (!enabled && configManager.viewerEnlarged) shrinkEmbeddedPDFViewer();
        updateTexpresso();
    });
    connect(texpressoManager, &TexpressoManager::updated, this, [this] {
        setPreviewPanelVisible(texpressoManager->current() && texpressoAction->isChecked());
    });
    connect(texpressoManager, &TexpressoManager::sourceLocation, this, [this](const QString &file, int line, int column, const QString &word) {
        Q_UNUSED(column)
        syncFromPreview(file, qMax(0, line - 1), true, word, PreviewSourceVersion::CurrentBuffer);
    });
    connect(texpressoWidget, &TexpressoWidget::syncRequested, this, [this] {
        if (auto editor = currentEditor()) syncTexpressoViewer(editor->cursor(), true);
    });
    connect(texpressoWidget, &TexpressoWidget::buildRequested, this, [this] {
        // The tab may have changed during the activation debounce. Never run a
        // recipe for a different project from the one shown in the live panel.
        const auto session = texpressoManager->current();
        const auto root = documents.getRootDocumentForDoc();
        if (!session || !root || TexpressoSession::identity(root->getFileName()) != session->root()) {
            updateTexpresso();
            setStatusMessageProcess(tr("The active project changed. Click Build & Refresh again to build its live preview."));
            return;
        }
        // This is the normal Compile action: it saves using the user's build
        // settings, honors compiler/bibliography magic comments, and lets the
        // BuildManager run additional TeX/BibTeX/Biber passes. Its sequence
        // signals above pause and refresh the captured root, even after a tab
        // switch. runCommand also rejects an already running command.
        runCommand(BuildManager::CMD_COMPILE);
    });
    connect(&documents, &LatexDocuments::masterDocumentChanged, this, [this] { texpressoActivationTimer.start(); });
    connect(editors, &Editors::listOfEditorsChanged, this, [this] { texpressoActivationTimer.start(); });
    connect(&documents, &LatexDocuments::aboutToDeleteDocument, this, [this](LatexDocument *document) {
        const auto path = document->property("texpressoPath").toString();
        if (!path.isEmpty()) texpressoManager->closeBuffer(path);
        texpressoActivationTimer.start();
    });
    for (auto document : documents.getDocuments())
        if (document->getEditorView()) watchTexpressoDocument(document);
    texpressoActivationTimer.start();
}

void Texstudio::watchTexpressoDocument(LatexDocument *document)
{
    if (texpressoDocuments.contains(document)) return;
    texpressoDocuments.insert(document);
    document->setProperty("texpressoPath", document->getFileName());
    texpressoManager->openBuffer(document->getFileName(), bufferText(document));
    connect(document, &QObject::destroyed, texpressoManager, [this, document] { texpressoDocuments.remove(document); });
    connect(document, &QDocument::changedText, texpressoManager,
        [this, document](int sl, int sc, int el, int ec, const QString &text) {
            if (!texpressoManager->changeBuffer(document->getFileName(), sl, sc, el, ec, text))
                texpressoManager->reconcileBuffer(document->getFileName(), bufferText(document));
        });
    auto reconcile = new QTimer(document);
    reconcile->setSingleShot(true);
    connect(reconcile, &QTimer::timeout, texpressoManager, [this, document] {
        // Keep a full check after the ordered deltas. Reloads, collaboration
        // edits and delayed macros can omit or defer changedText signals.
        texpressoManager->reconcileBuffer(document->getFileName(), bufferText(document));
    });
    connect(document, &QDocument::contentsChanged, reconcile, [reconcile] {
        // A selection replacement emits erase + insert, and a macro can emit
        // many changes. Capture once per event batch, without delaying deltas.
        if (!reconcile->isActive()) reconcile->start();
    });
    connect(document, &LatexDocument::structureUpdated, texpressoManager, [this] { texpressoActivationTimer.start(); });
    if (auto view = document->getEditorView()) {
        connect(view->editor, &QEditor::cursorPositionChanged, texpressoManager, [this, document, view] {
            if (currentEditorView() != view) return;
            const auto cursor = view->editor->cursor();
            const auto start = cursor.hasSelection() ? cursor.selectionStart() : cursor;
            const auto session = texpressoManager->current();
            const auto root = documents.getRootDocumentForDoc(document);
            if (session && root && TexpressoSession::identity(root->getFileName()) == session->root())
                session->prepareEdit(document->getFileName(), start.lineNumber(), start.columnNumber());
            syncPDFViewer(cursor, false);
        });
        connect(view->editor, &QEditor::saved, texpressoManager, [this] { texpressoActivationTimer.start(); });
        connect(view->editor, &QEditor::fileReloaded, texpressoManager, [this] { texpressoActivationTimer.start(); });
    }
    texpressoActivationTimer.start();
}

void Texstudio::updateTexpresso()
{
    if (!texpressoManager) return;
    QSet<QString> roots;
    for (auto document : documents.documents) {
        if (!document->getEditorView()) continue;
        watchTexpressoDocument(document);
        const auto path = document->getFileName();
        const auto previous = document->property("texpressoPath").toString();
        if (previous != path) {
            if (!previous.isEmpty()) texpressoManager->closeBuffer(previous);
            document->setProperty("texpressoPath", path);
            texpressoManager->openBuffer(path, bufferText(document));
        }
        const auto root = documents.getRootDocumentForDoc(document);
        if (root && !root->getFileName().isEmpty()) roots.insert(TexpressoSession::identity(root->getFileName()));
    }
    texpressoManager->retainProjects(roots);
    const auto root = documents.getRootDocumentForDoc();
    if (configManager.viewerEnlarged && root && TexpressoSession::identity(root->getFileName()) != previewPanel->project())
        shrinkEmbeddedPDFViewer();
    previewPanel->setProject(root && !root->getFileName().isEmpty() ? TexpressoSession::identity(root->getFileName()) : QString());
    texpressoManager->setPreviewVisible(previewPanel->liveVisible());
    texpressoManager->activate(texpressoAction->isChecked() && root ? root->getFileName() : QString());
    if (configManager.viewerEnlarged) {
        if (!previewPanel->isVisible() || previewPanel->mode() == ViewerPanel::Live) shrinkEmbeddedPDFViewer();
        else if (!enlargedViewer) enlargeEmbeddedPDFViewer();
    }
    if (auto session = texpressoManager->current()) {
        session->followCursor = configManager.pdfDocumentConfig->followFromCursor;
        if (auto editor = currentEditor()) {
            const auto cursor = editor->cursor();
            const auto start = cursor.hasSelection() ? cursor.selectionStart() : cursor;
            if (auto document = qobject_cast<LatexDocument *>(cursor.document())) {
                session->prepareEdit(document->getFileName(), start.lineNumber(), start.columnNumber());
                session->syncForward(document->getFileName(), cursor.lineNumber(), false, cursor.columnNumber());
            }
        }
    }
    texpressoWidget->refresh();
}

void Texstudio::ensurePreviewPdf()
{
#ifndef NO_POPPLER_PREVIEW
    if (!previewPanel || previewPanel->project().isEmpty()) return;
    const QFileInfo master(previewPanel->project());
    const auto file = buildManager.findCompiledFile(master.completeBaseName() + ".pdf", master);
    if (!QFileInfo(file).isReadable()) return;
    // Retain an existing viewer's page, zoom, search and PDF/SyncTeX pair.
    if (auto viewer = qobject_cast<PDFDocument *>(previewPanel->pdf(previewPanel->project()))) {
        viewer->loadFile(file, master, PDFDocument::NoDisplayFlags);
        return;
    }
    runInternalPdfViewer(master, enquoteStr(file) + " --embedded --preserve-existing-windowed --no-focus --no-foreground");
#endif
}

void Texstudio::setPreviewPanelVisible(bool visible)
{
    const bool opening = visible && previewPanel->isHidden();
    if (!visible && previewPanel->isVisible()) {
        const auto sizes = mainHSplitter->sizes();
        if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 10)
            pdfSplitterRel = qreal(sizes[1]) / (sizes[0] + sizes[1]);
    }
    previewPanel->setVisible(visible);
    if (opening) {
        const int width = mainHSplitter->width();
        const auto fraction = pdfSplitterRel >= .1 && pdfSplitterRel <= .9 ? pdfSplitterRel : .5;
        mainHSplitter->setSizes({qRound(width * (1 - fraction)), qRound(width * fraction)});
    }
}

void Texstudio::attachPreviewPdf(PDFDocument *viewer)
{
#ifndef NO_POPPLER_PREVIEW
    if (!previewPanel || !viewer->embeddedMode) return;
    viewer->previewPanelManaged = true;
    viewer->setParent(previewPanel); viewer->hide();
    const auto registerPdf = [this, viewer] {
        if (!viewer->getMasterFile().filePath().isEmpty())
            previewPanel->addPdf(TexpressoSession::identity(viewer->getMasterFile().absoluteFilePath()), viewer);
    };
    connect(viewer, &PDFDocument::documentLoaded, this, registerPdf);
    connect(viewer, &PDFDocument::closeRequested, this, [this] { texpressoAction->setChecked(false); focusEditor(); });
    connect(viewer, &PDFDocument::displayRequested, this, [this, viewer, registerPdf](PDFDocument::DisplayFlags flags) {
        registerPdf();
        const auto root = TexpressoSession::identity(viewer->getMasterFile().absoluteFilePath());
        // A compile-and-view recipe refreshes the cached PDF without taking
        // over Live mode. Explicit View PDF and focus requests select it.
        if ((flags & PDFDocument::Focus) || ((flags & PDFDocument::Raise) && !previewBuildDepth.contains(root))) {
            texpressoAction->setChecked(true);
            previewPanel->setProject(root); previewPanel->setMode(ViewerPanel::Pdf); setPreviewPanelVisible(true);
            if (flags & PDFDocument::Focus) viewer->widget()->setFocus();
        }
    });
#else
    Q_UNUSED(viewer)
#endif
}

bool Texstudio::syncTexpressoViewer(const QDocumentCursor &cursor, bool inForeground)
{
    if (!texpressoManager || !texpressoWidget->isVisible()) return false;
    auto session = texpressoManager->current();
    if (!session) return false;
    auto document = qobject_cast<LatexDocument *>(cursor.document());
    if (!document) return false;
    auto root = documents.getRootDocumentForDoc(document);
    if (!root || TexpressoSession::identity(root->getFileName()) != session->root()) return false;
    session->followCursor = configManager.pdfDocumentConfig->followFromCursor;
    session->syncForward(document->getFileName(), cursor.lineNumber(), inForeground, cursor.columnNumber());
    if (inForeground) texpressoWidget->setFocus();
    return true;
}
