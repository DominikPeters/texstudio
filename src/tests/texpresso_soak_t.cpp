#include "texstudio_t.h"
#include "texstudio.h"
#include "editors.h"
#include <QtTest>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QPaintEvent>
#include <QWindow>
#include <csignal>
#include <cerrno>
#ifdef TEXSTUDIO_TEXPRESSO
#include "texpresso/texpressowidget.h"

namespace {
bool save(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray read(const QString &path)
{
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
int lineAt(const QString &text, int offset) { return text.left(offset).count('\n'); }
int columnAt(const QString &text, int offset) { return offset - text.lastIndexOf('\n', offset - 1) - 1; }

// Observe the real canvas paint path without changing production scheduling.
// Completion means return from Qt paintEvent, not compositor presentation.
class SoakObserver : public QObject {
public:
    TexpressoManager *manager;
    TexpressoWidget *widget;
    QString directory, phase = "initializing";
    QElapsedTimer clock;
    QJsonArray inputs, events, probes;
    bool passed = false, capturing = false;
    struct Target { QString path; int line = -1, page = -1; qint64 sequence = -1, revision = -1, serial = 0; QPointF point; };
    QHash<QString, Target> targets;
    QSet<TexpressoSession *> observed;
    QHash<QString, int> inputIndex;
    QTimer heartbeat;
    SoakObserver(TexpressoManager *m, TexpressoWidget *w, const QString &d) : manager(m), widget(w), directory(d)
    {
        clock.start(); qApp->installEventFilter(this);
        heartbeat.setInterval(1000);
        connect(&heartbeat, &QTimer::timeout, this, [this] { checkpoint("sample"); });
        heartbeat.start();
    }
    double now() const { return clock.nsecsElapsed() / 1e6; }
    QString key(const QString &root, qint64 sequence) const { return root + ':' + QString::number(sequence); }
    void observe(TexpressoSession *session)
    {
        if (observed.contains(session)) return;
        observed.insert(session);
        connect(&session->preview, &TexpressoPreview::command, this, [this, session](const QJsonArray &c) {
            if (c[0] == "preview-search") {
                auto &t = targets[session->root()];
                t.path = TexpressoSession::identity(c[2].toString()); t.line = c[3].toInt();
            }
        });
        connect(&session->preview, &TexpressoPreview::reveal, this, [this, session](QPointF point) {
            auto &t = targets[session->root()]; ++t.serial;
            t.sequence = session->preview.sequence(); t.revision = session->preview.revision();
            t.page = session->preview.page(); t.point = point;
            const auto k = key(session->root(), t.sequence);
            if (inputIndex.contains(k)) {
                const int index = inputIndex[k]; auto input = inputs[index].toObject();
                if (input["path"] == t.path && input["line1"].toInt() == t.line && !input.contains("sourceReadyMs")) {
                    input["sourceReadyMs"] = now(); inputs[index] = input;
                }
            }
        });
    }
    void input(const QString &path, int line, double scheduled)
    {
        const auto s = manager->current();
        inputIndex[key(s->root(), s->preview.sequence())] = inputs.size();
        inputs.append(QJsonObject{{"phase", phase}, {"path", TexpressoSession::identity(path)}, {"line1", line + 1},
            {"root", s->root()}, {"sequence", double(s->preview.sequence())}, {"scheduledMs", scheduled},
            {"appliedMs", now()}, {"latenessMs", now() - scheduled}});
    }
    void checkpoint(const QString &name)
    {
        auto s = manager->current(); QJsonArray pids;
        for (auto session : observed) if (session->processId() > 0) pids.append(double(session->processId()));
        int visible = 0; qint64 bytes = 0;
        for (auto c : widget->findChildren<TexpressoCanvas *>()) {
            visible += c->isVisible(); bytes += c->pixels.sizeInBytes();
        }
        const QJsonObject row{{"atMs", now()}, {"phase", phase}, {"name", name}, {"sessionProcessIds", pids},
            {"sessions", manager->sessionCount()}, {"visibleCanvases", visible}, {"canvasReferencedBytes", double(bytes)},
            {"root", s ? s->root() : QString()}, {"page", s ? s->preview.page() : -1},
            {"currentFrame", s && s->preview.valid()}, {"windowExposed", widget->window()->windowHandle() && widget->window()->windowHandle()->isExposed()}};
        events.append(row); save(directory + "/heartbeat.json", QJsonDocument(row).toJson());
    }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto canvas = qobject_cast<TexpressoCanvas *>(object);
        auto session = manager->current();
        if (capturing || !canvas || event->type() != QEvent::Paint || !session || !canvas->fresh || !session->preview.valid()) return false;
        const auto target = targets.value(session->root()); const auto &preview = session->preview;
        if (target.sequence != preview.sequence() || target.revision != preview.revision() || target.page != preview.page()
            || canvas->pixels.cacheKey() != preview.image().cacheKey()) return false;
        auto area = widget->findChild<QScrollArea *>(); if (!area) return false;
        const auto local = ((target.point - canvas->pageBounds.topLeft()) * canvas->scale).toPoint();
        if (!area->viewport()->rect().contains(canvas->mapTo(area->viewport(), local))
            || !static_cast<QPaintEvent *>(event)->region().contains(local)) return false;
        const auto k = key(session->root(), preview.sequence());
        if (!inputIndex.contains(k)) return false;
        const int index = inputIndex[k]; const auto input = inputs[index].toObject();
        if (input.contains("paintCompletedMs") || input["path"] != target.path || input["line1"].toInt() != target.line) return false;
        const QPointer<TexpressoSession> guarded(session);
        const double started = now();
        QTimer::singleShot(0, this, [this, guarded, index, target, started] {
            if (!guarded || manager->current() != guarded || guarded->preview.sequence() != target.sequence
                || guarded->preview.revision() != target.revision || !guarded->preview.valid()) return;
            auto input = inputs[index].toObject(); if (input.contains("paintCompletedMs")) return;
            input["paintStartedMs"] = started; input["paintCompletedMs"] = now(); input["page"] = target.page;
            inputs[index] = input;
        });
        return false;
    }
    ~SoakObserver() override
    {
        qApp->removeEventFilter(this);
        save(directory + "/ui-result.json", QJsonDocument(QJsonObject{{"schema", 1}, {"passed", passed},
            {"elapsedMs", now()}, {"platform", QGuiApplication::platformName()}, {"inputs", inputs},
            {"checkpoints", events}, {"probes", probes},
            {"endpoint", "Fresh source target in visible canvas paintEvent, then next Qt event turn; excludes physical keyboard/compositor latency"}}).toJson());
        for (auto s : observed) {
            const auto name = QFileInfo(s->root()).baseName();
            save(directory + '/' + name + "-engine.log", s->engineOutput.toUtf8());
            save(directory + '/' + name + "-compiler.log", s->compilerOutput.toUtf8());
        }
    }
};
}
#endif

extern Texstudio *txsInstance;
void TexStudioTest::liveEditingSoak()
{
#ifndef TEXSTUDIO_TEXPRESSO
    QSKIP("Live preview is not built");
#else
    if (qEnvironmentVariableIntValue("TEXPRESSO_TEST_UI_SOAK") != 1) QSKIP("Opt in with native/scripts/ui_soak.py");
    const auto output = qEnvironmentVariable("TEXPRESSO_UI_SOAK_ARTIFACTS");
    QVERIFY(!output.isEmpty()); QVERIFY(QDir().mkpath(output + "/project")); QVERIFY(QDir().mkpath(output + "/final"));
    bool durationValid = false; const int seconds = qEnvironmentVariable("TEXPRESSO_UI_SOAK_SECONDS", "600").toInt(&durationValid);
    QVERIFY(durationValid && seconds >= 10 && seconds <= 3600);
    const auto root = output + "/project/main.tex", child = output + "/project/chapter.tex", other = output + "/project/other.tex";
    const int targetPages = qEnvironmentVariableIntValue("TEXPRESSO_UI_SOAK_PAGES");
    QVERIFY(targetPages == 30 || targetPages == 100);
    QString source = QString::fromUtf8(read(QString(TESTDATADIR)
        + QString("/../../texpresso/tests/sustained/varied-%1/varied.tex").arg(targetPages)));
    QVERIFY(!source.isEmpty());
    const bool externalUpdates = qEnvironmentVariableIntValue("TEXPRESSO_UI_SOAK_EXTERNAL_UPDATES") == 1;
    const auto external = output + "/project/external.tex";
    if (externalUpdates) {
        source.replace("\\begin{document}", "\\input{external}\n\\begin{document}");
        QVERIFY(save(external, "\\typeout{UIEXTERNAL0000}\n"));
    }
    const QStringList tokens{"UIEARLY", "UIMIDDLE", "UILATE", "UIINCLUDE"};
    for (const auto &anchor : {QString("early"), QString("middle"), QString("late")}) {
        const auto token = "UI" + anchor.toUpper();
        source.replace("%<VARIED-CURSOR:" + anchor + '>', "\\noindent \\texttt{" + token + "0000}.\n"
            + (anchor == "middle" ? QString("\\input{chapter}\n") : QString()) + "%<VARIED-CURSOR:" + anchor + '>');
    }
    QVERIFY(save(root, source.toUtf8()));
    const QByteArray diskChild = "% !TeX root = main.tex\n\\noindent \\texttt{UIINCLUDE0000}. Unsaved included source.\n";
    QVERIFY(save(child, diskChild));
    QVERIFY(save(other, "\\documentclass{article}\n\\begin{document}\n\\texttt{UIOTHER0000}. Another project.\\newpage Second page.\\end{document}\n"));
    auto txs = txsInstance; auto preview = txs->texpressoWidget; auto manager = txs->texpressoManager;
    const bool oldEnabled = txs->texpressoAction->isChecked(), oldFollow = txs->configManager.pdfDocumentConfig->followFromCursor;
    QList<LatexEditorView *> views;
    const auto cleanup = qScopeGuard([&] {
        for (auto view : views) { txs->editors->setCurrentEditor(view); view->document->setClean(); txs->fileClose(); }
        txs->configManager.pdfDocumentConfig->followFromCursor = oldFollow;
        txs->texpressoAction->setChecked(oldEnabled);
    });
    txs->resize(1400, 950); txs->show(); txs->texpressoAction->setChecked(true);
    SoakObserver observer(manager, preview, output); observer.checkpoint("started");
    const auto snapshots = qScopeGuard([&] {
        QDir().mkpath(output + "/editor-snapshots");
        for (auto view : views)
            save(output + "/editor-snapshots/" + QFileInfo(view->document->getFileName()).fileName(), view->editor->text().toUtf8());
        observer.checkpoint(observer.passed ? "passed" : "failed");
    });
    auto mainView = txs->load(root); QVERIFY(mainView); views << mainView;
    auto childView = txs->load(child); QVERIFY(childView); views << childView;
    auto otherView = txs->load(other); QVERIFY(otherView); views << otherView;
    auto select = [&](LatexEditorView *view) {
        txs->editors->setCurrentEditor(view); view->editor->setFocus();
    };
    const auto ready = [&] { auto s = manager->current(); return s && s->preview.valid() && !s->failed() && s->compilerError.isEmpty(); };
    select(mainView);
    QTRY_VERIFY_WITH_TIMEOUT(manager->current() && manager->current()->root() == TexpressoSession::identity(root) && ready(), 60000);
    auto mainSession = manager->current(); observer.observe(mainSession);
    preview->viewerActions()["followCursor"]->setChecked(true);
    auto click = [&](const char *name) {
        auto button = preview->findChild<QToolButton *>(name); if (!button || !button->isEnabled()) return false;
        QTest::mouseClick(button, Qt::LeftButton); return true;
    };
    auto trigger = [&](const char *id) {
        auto action = preview->viewerActions().value(id); if (!action || !action->isEnabled()) return false;
        action->trigger(); return true;
    };
    auto position = [&](LatexEditorView *view, const QString &needle, bool number) {
        const auto text = view->editor->text(); const int at = text.indexOf(needle);
        if (at < 0) return false;
        const int start = at + (number ? needle.size() : 0);
        QDocumentCursor cursor(view->editor->document(), lineAt(text, start), columnAt(text, start));
        if (number) cursor.movePosition(4, QDocumentCursor::NextCharacter, QDocumentCursor::KeepAnchor);
        view->editor->setCursor(cursor); return true;
    };
    auto restart = [&] {
        auto s = manager->current(); const auto oldPid = s->processId();
        if (!trigger("stop")) return false;
        QElapsedTimer deadline; deadline.start();
        while (!(::kill(-oldPid, 0) == -1 && errno == ESRCH) && deadline.elapsed() < 5000) QTest::qWait(20);
        if (!(::kill(-oldPid, 0) == -1 && errno == ESRCH) || !trigger("restart")) return false;
        deadline.restart();
        while (!(ready() && s->processId() != oldPid) && deadline.elapsed() < 30000) QTest::qWait(20);
        return ready() && s->processId() != oldPid;
    };
    qint64 sustainedPid = 0;
    QHash<QString, int> values; int cycles = 0;
    while (!cycles || observer.clock.elapsed() < seconds * 1000) {
        if (cycles) QCOMPARE(mainSession->processId(), sustainedPid);
        for (const auto &token : tokens) {
            observer.phase = QString("cycle-%1-%2").arg(cycles).arg(token);
            auto view = token == "UIINCLUDE" ? childView : mainView; select(view);
            QTRY_VERIFY_WITH_TIMEOUT(manager->current() == mainSession && ready(), 30000);
            QTimer timer; timer.setSingleShot(true); timer.setTimerType(Qt::PreciseTimer);
            const double start = observer.now(); int edits = 0; bool failed = false;
            connect(&timer, &QTimer::timeout, &timer, [&] {
                if (!position(view, token, true)) { failed = true; return; }
                view->editor->write(QString("%1").arg(++values[token] % 10000, 4, 10, QChar('0')));
                observer.input(view->document->getFileName(), view->editor->cursor().lineNumber(), start + edits * 100);
                if (++edits < 32) timer.start(qMax(0, qRound(start + edits * 100 - observer.now())));
            });
            timer.start(0);
            QTRY_VERIFY_WITH_TIMEOUT(edits == 32 || failed, 15000); QVERIFY(!failed);
            QTRY_VERIFY_WITH_TIMEOUT(ready() && observer.inputs.last().toObject().contains("paintCompletedMs"), 30000);
            observer.checkpoint("typing-settled");
        }
        QVERIFY(read(child) == diskChild); // The include stays dirty through all recovery paths.
        observer.phase = "scroll-zoom-resize";
        preview->viewerActions()["continuous"]->setChecked(true);
        auto area = preview->findChild<QScrollArea *>(); QVERIFY(area);
        const int before = area->verticalScrollBar()->value();
        QWheelEvent wheel(QPointF(area->viewport()->rect().center()), QPointF(area->viewport()->mapToGlobal(area->viewport()->rect().center())),
            QPoint(), QPoint(0, -480), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(area->viewport(), &wheel); QTest::qWait(200);
        QVERIFY(area->verticalScrollBar()->value() != before);
        preview->viewerActions()["zoomIn"]->trigger(); QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000);
        txs->resize(1200 + (cycles % 2) * 160, 850 + (cycles % 2) * 80); QTest::qWait(200);
        preview->viewerActions()["fitToWidth"]->trigger(); QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000);
        observer.phase = "project-switch"; const auto retainedPid = mainSession->processId(); select(otherView);
        QTRY_VERIFY_WITH_TIMEOUT(manager->current() && manager->current()->root() == TexpressoSession::identity(other) && ready(), 30000);
        observer.observe(manager->current()); QCOMPARE(manager->sessionCount(), 2);
        QVERIFY(position(otherView, "UIOTHER", true)); otherView->editor->write(QString("%1").arg((cycles + 1) % 10000, 4, 10, QChar('0')));
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000); select(mainView);
        QTRY_VERIFY_WITH_TIMEOUT(manager->current() == mainSession && ready(), 30000); QCOMPARE(mainSession->processId(), retainedPid);
        observer.phase = "error-undo-redo-repair"; QVERIFY(position(mainView, "UIEARLY", false));
        const auto good = mainView->editor->text();
        // Group on the document: a copied cursor would add an empty undo step
        // because QEditor::write inserts through the editor's own cursor.
        mainView->document->beginMacro(); mainView->editor->write("\\uiSoakUndefined{}"); mainView->document->endMacro();
        QVERIFY(!mainView->document->hasMacros()); QVERIFY(mainView->editor->canUndo());
        QTRY_VERIFY_WITH_TIMEOUT(!mainSession->compilerError.isEmpty(), 30000); QVERIFY(!mainSession->failed());
        observer.checkpoint("compile-error");
        mainView->editor->undo(); QCOMPARE(mainView->editor->text(), good); QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000);
        observer.checkpoint("undo-recovered");
        mainView->editor->redo(); QTRY_VERIFY_WITH_TIMEOUT(!mainSession->compilerError.isEmpty(), 30000);
        observer.checkpoint("redo-error");
        mainView->editor->undo(); QCOMPARE(mainView->editor->text(), good); QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000);
        observer.phase = "pause-resume"; QVERIFY(click("texpressoPause")); QVERIFY(mainSession->userPaused);
        QVERIFY(position(childView, "UIINCLUDE", true)); select(childView);
        childView->editor->write(QString("%1").arg(++values["UIINCLUDE"] % 10000, 4, 10, QChar('0')));
        QVERIFY(click("texpressoPause")); QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000);
        observer.phase = "hide-restore"; txs->texpressoAction->setChecked(false); QTest::qWait(150);
        QVERIFY(!preview->isVisible()); txs->texpressoAction->setChecked(true); QTRY_VERIFY_WITH_TIMEOUT(preview->isVisible() && ready(), 30000);
        if (!cycles) {
            observer.phase = "stop-restart"; QVERIFY(restart());
            sustainedPid = mainSession->processId(); observer.checkpoint("sustained-session-started");
        }
        QCOMPARE(mainSession->processId(), sustainedPid);
        if (externalUpdates) {
            observer.phase = "external-file-change";
            const auto marker = QString("UIEXTERNAL%1").arg(cycles + 1, 4, 10, QChar('0'));
            QVERIFY(save(external, ("\\typeout{" + marker + "}\n").toUtf8()));
            QTRY_VERIFY_WITH_TIMEOUT(ready() && mainSession->compilerOutput.contains(marker), 30000);
            observer.checkpoint("external-file-recompiled");
            observer.phase = "external-build-reset";
            const auto oldPid = mainSession->processId();
            manager->beginExternalBuild(root);
            QVERIFY(mainSession->externalBuildRunning());
            QVERIFY(save(QFileInfo(root).path() + "/main.aux", "\\relax\n% Auxiliary output from the simulated full build.\n"));
            manager->endExternalBuild(root, true);
            QTRY_VERIFY_WITH_TIMEOUT(ready() && mainSession->processId() != oldPid, 30000);
            QTRY_VERIFY_WITH_TIMEOUT(::kill(-oldPid, 0) == -1 && errno == ESRCH, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(mainSession->compilerOutput.contains(marker), 30000);
            sustainedPid = mainSession->processId();
            observer.checkpoint("external-build-recovered");
        }
        QVERIFY(read(child) == diskChild); observer.checkpoint("cycle-settled");
        observer.capturing = true; QVERIFY(preview->grab().save(output + QString("/cycle-%1.png").arg(cycles))); observer.capturing = false;
        ++cycles;
    }
    // Replay the accumulated unsaved edits after a long-lived compiler session.
    observer.phase = "final-stop-restart"; QVERIFY(restart()); observer.checkpoint("final-restart-settled");
    observer.phase = "final-probes";
    auto probeTokens = tokens; probeTokens << "UIOTHER";
    for (const auto &token : probeTokens) {
        auto view = token == "UIOTHER" ? otherView : token == "UIINCLUDE" ? childView : mainView; select(view);
        const auto expectedRoot = TexpressoSession::identity(token == "UIOTHER" ? other : root);
        QTRY_VERIFY_WITH_TIMEOUT(manager->current() && manager->current()->root() == expectedRoot && ready(), 30000);
        auto s = manager->current(); QVERIFY(position(view, token, true));
        const auto serial = observer.targets.value(s->root()).serial; QVERIFY(click("texpressoSync"));
        QTRY_VERIFY_WITH_TIMEOUT(observer.targets.value(s->root()).serial > serial && ready(), 30000);
        const auto text = view->editor->text(); const int at = text.indexOf(token);
        observer.probes.append(QJsonObject{{"root", QFileInfo(s->root()).fileName()}, {"file", QFileInfo(view->document->getFileName()).fileName()},
            {"line1", lineAt(text, at) + 1}, {"token", text.mid(at, token.size() + 4)}, {"page", s->preview.page()}});
    }
    for (auto view : views) QVERIFY(save(output + "/final/" + QFileInfo(view->document->getFileName()).fileName(), view->editor->text().toUtf8()));
    if (externalUpdates) QVERIFY(save(output + "/final/external.tex", read(external)));
    QVERIFY(mainView->editor->isContentModified()); QVERIFY(childView->editor->isContentModified());
    QVERIFY(read(child) == diskChild); QVERIFY(!observer.inputs.isEmpty());
    int painted = 0; for (const auto &value : observer.inputs) painted += value.toObject().contains("paintCompletedMs");
    QVERIFY2(painted > 0, "No current source target reached the real Qt canvas paint path");
    observer.checkpoint("finished"); observer.passed = true;
#endif
}

void TexStudioTest::liveEditorBuffers()
{
#ifndef TEXSTUDIO_TEXPRESSO
    QSKIP("Live preview is not built");
#else
    auto txs = txsInstance; auto manager = txs->texpressoManager;
    const bool enabled = txs->texpressoAction->isChecked();
    txs->texpressoAction->setChecked(false); txs->updateTexpresso();
    QTemporaryDir directory; const auto path = directory.filePath("buffer.tex");
    QVERIFY(save(path, "First line\nUnicode: café 😀\nLast line\n"));
    auto view = txs->load(path); QVERIFY(view); txs->updateTexpresso();
    const auto cleanup = qScopeGuard([&] {
        view->document->setClean(); txs->editors->setCurrentEditor(view); txs->fileClose();
        txs->texpressoAction->setChecked(enabled); txs->updateTexpresso();
    });
    auto mirror = [&] { return manager->buffers.value(TexpressoSession::identity(path)); };
    auto actual = [&] { return view->document->textLines().join('\n'); };
    QCOMPARE(mirror(), actual());
    auto replace = [&](int sl, int sc, int el, int ec, const QString &text) {
        QDocumentCursor cursor(view->document, sl, sc); cursor.moveTo(el, ec, QDocumentCursor::KeepAnchor);
        view->editor->setCursor(cursor); view->editor->write(text);
    };
    replace(1, 14, 1, 16, "λ😀\nSecond inserted line");
    QCOMPARE(mirror(), actual()); // Ordered deltas are retained synchronously.
    view->editor->undo(); QCOMPARE(mirror(), actual());
    view->editor->redo(); QCOMPARE(mirror(), actual());
    replace(0, 0, 2, 3, "Multiple\nlines\nchanged\n"); QCOMPARE(mirror(), actual());
    // Reload without range signals, then another edit before the queued check.
    // The delta must not make the manager mistake a stale base for a full copy.
    view->editor->setText("Reloaded base\nDifferent second line\n", false);
    replace(0, 0, 0, 1, "R");
    QCoreApplication::processEvents(); QCOMPARE(mirror(), actual());
    // Invalid coordinates never modify retained state; the adapter can snapshot.
    const auto before = mirror();
    QVERIFY(!manager->changeBuffer(path, 0, 999, 0, 999, "x"));
    QVERIFY(!manager->changeBuffer(path, 1, 0, 0, 0, "x"));
    QVERIFY(!manager->changeBuffer(path, 99, 0, 99, 0, "x"));
    QCOMPARE(mirror(), before);
    replace(2, 0, 2, 0, "At final empty line"); QCOMPARE(mirror(), actual());
    view->editor->setText("Replacement without ranges\n", false);
    QCoreApplication::processEvents(); QCOMPARE(mirror(), actual());
#endif
}

void TexStudioTest::liveEditingBufferBenchmark()
{
#ifndef TEXSTUDIO_TEXPRESSO
    QSKIP("Live preview is not built");
#else
    if (qEnvironmentVariableIntValue("TEXPRESSO_TEST_UI_BENCHMARK") != 1)
        QSKIP("Opt in with TEXPRESSO_TEST_UI_BENCHMARK=1");
    const auto output = qEnvironmentVariable("TEXPRESSO_UI_BENCHMARK_OUTPUT"); QVERIFY(!output.isEmpty());
    auto txs = txsInstance; auto manager = txs->texpressoManager;
    const bool enabled = txs->texpressoAction->isChecked();
    txs->texpressoAction->setChecked(false); txs->updateTexpresso();
    QJsonArray runs;
    for (int pages : {30, 100}) {
        const auto source = QString::fromUtf8(read(QString(TESTDATADIR)
            + QString("/../../texpresso/tests/sustained/varied-%1/varied.tex").arg(pages)));
        QVERIFY(!source.isEmpty()); QTemporaryDir directory; const auto path = directory.filePath("main.tex");
        QVERIFY(save(path, source.toUtf8())); auto view = txs->load(path); QVERIFY(view); txs->updateTexpresso();
        const auto close = qScopeGuard([&] { view->document->setClean(); txs->editors->setCurrentEditor(view); txs->fileClose(); });
        QCoreApplication::processEvents();
        for (const auto &anchor : {QString("early"), QString("middle"), QString("late")}) {
            const int at = source.indexOf("%<VARIED-CURSOR:" + anchor + '>'); QVERIFY(at >= 0);
            const int line = lineAt(source, at), column = columnAt(source, at) + 3;
            QJsonArray writes, turns;
            for (int i = 0; i < 110; ++i) {
                QDocumentCursor cursor(view->document, line, column);
                cursor.movePosition(4, QDocumentCursor::NextCharacter, QDocumentCursor::KeepAnchor);
                view->editor->setCursor(cursor);
                QElapsedTimer clock; clock.start();
                view->editor->write(QString("%1").arg(i % 10000, 4, 10, QChar('0')));
                const double write = clock.nsecsElapsed() / 1e6;
                QCoreApplication::processEvents(); const double total = clock.nsecsElapsed() / 1e6;
                if (i >= 10) { writes.append(write); turns.append(total); }
            }
            QCOMPARE(manager->buffers.value(TexpressoSession::identity(path)), view->document->textLines().join('\n'));
            runs.append(QJsonObject{{"pages", pages}, {"anchor", anchor}, {"sourceBytes", source.toUtf8().size()},
                {"writeMs", writes}, {"writeAndEventTurnMs", turns}});
        }
    }
    txs->texpressoAction->setChecked(enabled); txs->updateTexpresso();
    QVERIFY(save(output, QJsonDocument(QJsonObject{{"schema", 1}, {"platform", QGuiApplication::platformName()},
        {"method", "Actual QEditor four-character selection replacement; live compilation disabled; 10 warmups, 100 measured writes per location. Event-turn time includes other application work; no hard timing thresholds."},
        {"runs", runs}}).toJson()));
#endif
}
