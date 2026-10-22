// The editor-specific adapter is kept outside the protocol/session library.
#include "texstudio.h"
#include "texpresso/texpressomanager.h"
#include "texpresso/texpressowidget.h"
#include "minisplitter.h"
#include "qdocument.h"
#include "qdocumentcursor.h"
#include "qeditor.h"
#include "utilsSystem.h"
#include <QPointer>
#include <QSettings>
#include <QScreen>

namespace {
QString bufferText(LatexDocument *document) { return document->textLines().join('\n'); }
}

void Texstudio::setupTexpresso()
{
    texpressoManager = new TexpressoManager(this);
    // BuildManager captures the root before launching the recipe, so switching
    // editor tabs while LaTeX/BibTeX/Biber run cannot refresh another project.
    connect(&buildManager, &BuildManager::buildSequenceStarted, texpressoManager,
            [this](const QString &root, QStringList directories, bool compilerSequence) {
        // A standalone bibliography tool may omit the compiler's output path.
        // A compiler recipe is authoritative, including returning to root output.
        if (directories.isEmpty() && !compilerSequence) directories = texpressoManager->auxiliaryDirectories(root);
        texpressoManager->beginExternalBuild(root, directories);
    });
    connect(&buildManager, &BuildManager::buildSequenceFinished, texpressoManager,
            [this](const QString &root, bool success) {
        texpressoManager->endExternalBuild(root, success);
    });
    auto previewConfig = configManager.pdfDocumentConfig;
    configManager.registerOption("Preview/EditTool", &previewConfig->editTool, 1);
    configManager.registerOption("Preview/Scrolling Follows Cursor", &previewConfig->followFromCursor, false);
    configManager.registerOption("Preview/Invert Colors", &previewConfig->invertColors, false);
    configManager.registerOption("Preview/Grayscale", &previewConfig->grayscale, false);
    texpressoWidget = new TexpressoWidget(texpressoManager, previewConfig, getRealIcon, mainHSplitter);
    // Use the PDF action IDs so configured PDF shortcuts also reach the live
    // panel. Widget-scoped contexts prevent clashes with the editor or PDF.
    for (auto it = texpressoWidget->viewerActions().cbegin(); it != texpressoWidget->viewerActions().cend(); ++it) {
        const QString menu = it.key() == "gotoSource" ? "window" : it.key() == "followCursor" ? "config" : "view";
        it.value()->setObjectName("pdf/" + menu + '/' + it.key());
    }
    configManager.modifyManagedShortcuts("pdf");
    const int iconSize = qRound(qMax(16, configManager.guiSecondaryToolbarIconSize) * QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96);
    texpressoWidget->configureAppearance(configManager.getOption("GUI/Style").toInt() != 0, iconSize);
    connect(texpressoWidget, &TexpressoWidget::followCursorChanged, this, [this](bool enabled) {
        configManager.pdfDocumentConfig->followFromCursor = enabled;
        updateTexpresso();
        if (enabled && currentEditor()) syncPDFViewer(currentEditor()->cursor(), false);
    });
    connect(texpressoWidget, &TexpressoWidget::toolChanged, this, [this](int tool) { configManager.pdfDocumentConfig->editTool = tool; });
    mainHSplitter->addWidget(texpressoWidget); texpressoWidget->hide();
    texpressoAction = getManagedMenu("main/view/show")->addAction(tr("Live Preview"));
    texpressoAction->setObjectName("texpressoLivePreviewAction");
    texpressoAction->setCheckable(true);
    texpressoAction->setChecked(configManager.getSettings()->value("texpresso/enabled", true).toBool());
    texpressoActivationTimer.setSingleShot(true); texpressoActivationTimer.setInterval(75);
    connect(&texpressoActivationTimer, &QTimer::timeout, this, &Texstudio::updateTexpresso);
    connect(texpressoAction, &QAction::toggled, this, [this](bool enabled) {
        configManager.getSettings()->setValue("texpresso/enabled", enabled);
        updateTexpresso();
    });
    connect(texpressoManager, &TexpressoManager::updated, this, [this] {
        const bool show = texpressoManager->current() && texpressoAction->isChecked();
        const bool opening = show && texpressoWidget->isHidden();
        texpressoWidget->setVisible(show);
        if (opening) {
            const int width = mainHSplitter->width();
            mainHSplitter->setSizes({width / 2, width / 2});
        }
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
            texpressoManager->changeBuffer(document->getFileName(), sl, sc, el, ec, text, bufferText(document));
        });
    connect(document, &QDocument::contentsChanged, texpressoManager, [this, guarded = QPointer<LatexDocument>(document)] {
        // contentsChanged precedes changedText. Reconcile after the ordered range
        // signals, covering reloads and collaboration edits that suppress them.
        QTimer::singleShot(0, texpressoManager, [this, guarded] {
            if (guarded) texpressoManager->reconcileBuffer(guarded->getFileName(), bufferText(guarded));
        });
    });
    connect(document, &LatexDocument::structureUpdated, texpressoManager, [this] { texpressoActivationTimer.start(); });
    if (auto view = document->getEditorView()) {
        connect(view->editor, &QEditor::cursorPositionChanged, texpressoManager, [this, document, view] {
            if (currentEditorView() == view) syncPDFViewer(view->editor->cursor(), false);
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
    texpressoManager->activate(texpressoAction->isChecked() && root ? root->getFileName() : QString());
    if (auto session = texpressoManager->current()) session->followCursor = configManager.pdfDocumentConfig->followFromCursor;
    texpressoWidget->refresh();
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
    if (inForeground || session->followCursor) session->syncForward(document->getFileName(), cursor.lineNumber());
    if (inForeground) texpressoWidget->setFocus();
    return true;
}
