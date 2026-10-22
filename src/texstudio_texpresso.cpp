// The editor-specific adapter is kept outside the protocol/session library.
#include "texstudio.h"
#include "texpresso/texpressomanager.h"
#include "texpresso/texpressowidget.h"
#include "minisplitter.h"
#include "qdocument.h"
#include "qdocumentcursor.h"
#include "qeditor.h"
#include <QPointer>
#include <QSettings>

namespace {
QString bufferText(LatexDocument *document) { return document->textLines().join('\n'); }
}

void Texstudio::setupTexpresso()
{
    texpressoManager = new TexpressoManager(this);
    texpressoWidget = new TexpressoWidget(texpressoManager, mainHSplitter);
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
        Q_UNUSED(word)
        // Live SyncTeX refers to the current buffers, not saved PDF line snapshots.
        gotoLine(qMax(0, line - 1), file);
    });
    connect(texpressoWidget, &TexpressoWidget::syncRequested, this, [this] {
        if (auto session = texpressoManager->current()) {
            if (auto editor = currentEditor()) session->syncForward(getCurrentFileName(), editor->cursor().lineNumber());
        }
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
            if (currentEditorView() == view)
                if (auto session = texpressoManager->current()) session->syncForward(document->getFileName(), view->editor->cursor().lineNumber());
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
}
