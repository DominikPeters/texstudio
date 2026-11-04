#include "texstudio_t.h"
#include "texstudio.h"
#include "editors.h"
#include "utilsSystem.h"
#include <QtTest>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStyleHints>
#ifdef TEXSTUDIO_TEXPRESSO
#include "texpresso/texpressowidget.h"

namespace {
// Records when the live canvas last painted, so a frame is captured only once
// the preview has stopped changing after an edit.
class DemoPaintWatch : public QObject {
public:
    QElapsedTimer clock;
    qint64 last = -1;
    DemoPaintWatch() { clock.start(); qApp->installEventFilter(this); }
    ~DemoPaintWatch() override { qApp->removeEventFilter(this); }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Paint && qobject_cast<TexpressoCanvas *>(object)) last = clock.elapsed();
        return false;
    }
};
}
#endif

extern Texstudio *txsInstance;

// Opt-in: native/scripts/demo_capture.py writes the projects and an action
// list, then runs this with offscreen widgets to produce landing-page frames.
// Every frame is a grab of the real main window after the live preview settled.
void TexStudioTest::liveDemoCapture()
{
#ifndef TEXSTUDIO_TEXPRESSO
    QSKIP("Live preview is not built");
#else
    if (qEnvironmentVariableIntValue("TEXPRESSO_TEST_DEMO_CAPTURE") != 1) QSKIP("Opt in with native/scripts/demo_capture.py");
    const auto output = qEnvironmentVariable("TEXPRESSO_DEMO_OUTPUT");
    QVERIFY(!output.isEmpty()); QVERIFY(QDir().mkpath(output + "/frames"));
    QFile actionFile(qEnvironmentVariable("TEXPRESSO_DEMO_ACTIONS"));
    QVERIFY(actionFile.open(QIODevice::ReadOnly));
    const auto spec = QJsonDocument::fromJson(actionFile.readAll()).object();
    const auto actions = spec["actions"].toArray(); QVERIFY(!actions.isEmpty());

    auto txs = txsInstance; auto preview = txs->texpressoWidget; auto manager = txs->texpressoManager;
    const bool oldEnabled = txs->texpressoAction->isChecked();
    QHash<QString, LatexEditorView *> views;
    const auto cleanup = qScopeGuard([&] {
        for (auto view : views) { txs->editors->setCurrentEditor(view); view->document->setClean(); txs->fileClose(); }
        // closeOthers removed the runner's own empty document; after the tests
        // Texstudio::executeTests closes the current editor and needs one.
        if (!txs->currentEditorView()) txs->fileNew();
        txs->texpressoAction->setChecked(oldEnabled);
    });
    const auto size = spec["window"].toObject();
    txs->resize(size["width"].toInt(1440), size["height"].toInt(900)); txs->show();
    txs->texpressoAction->setChecked(true);
    if (spec["darkPalette"].toBool()) {
        // macOS dark mode reaches TeXstudio as an application palette change
        // (Texstudio::paletteChanged); the offscreen platform has no system
        // appearance, so apply colours close to the macOS dark palette.
        QPalette dark;
        const auto set = [&](QPalette::ColorRole role, const QColor &active, const QColor &disabled) {
            dark.setColor(QPalette::Active, role, active); dark.setColor(QPalette::Inactive, role, active);
            dark.setColor(QPalette::Disabled, role, disabled);
        };
        set(QPalette::Window, QColor(0x32, 0x32, 0x32), QColor(0x32, 0x32, 0x32));
        set(QPalette::WindowText, QColor(0xdf, 0xdf, 0xdf), QColor(0x7f, 0x7f, 0x7f));
        set(QPalette::Base, QColor(0x1e, 0x1e, 0x1e), QColor(0x1e, 0x1e, 0x1e));
        set(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26), QColor(0x26, 0x26, 0x26));
        set(QPalette::Text, QColor(0xdf, 0xdf, 0xdf), QColor(0x7f, 0x7f, 0x7f));
        set(QPalette::Button, QColor(0x3c, 0x3c, 0x3c), QColor(0x32, 0x32, 0x32));
        set(QPalette::ButtonText, QColor(0xdf, 0xdf, 0xdf), QColor(0x7f, 0x7f, 0x7f));
        set(QPalette::BrightText, QColor(0xff, 0xff, 0xff), QColor(0xff, 0xff, 0xff));
        set(QPalette::Light, QColor(0x50, 0x50, 0x50), QColor(0x50, 0x50, 0x50));
        set(QPalette::Midlight, QColor(0x46, 0x46, 0x46), QColor(0x46, 0x46, 0x46));
        set(QPalette::Mid, QColor(0x2a, 0x2a, 0x2a), QColor(0x2a, 0x2a, 0x2a));
        set(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a), QColor(0x1a, 0x1a, 0x1a));
        set(QPalette::Shadow, QColor(0x00, 0x00, 0x00), QColor(0x00, 0x00, 0x00));
        set(QPalette::Highlight, QColor(0x3f, 0x63, 0x8b), QColor(0x46, 0x46, 0x46));
        set(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff), QColor(0x7f, 0x7f, 0x7f));
        set(QPalette::ToolTipBase, QColor(0x3c, 0x3c, 0x3c), QColor(0x3c, 0x3c, 0x3c));
        set(QPalette::ToolTipText, QColor(0xdf, 0xdf, 0xdf), QColor(0x7f, 0x7f, 0x7f));
        set(QPalette::PlaceholderText, QColor(0x7f, 0x7f, 0x7f), QColor(0x5f, 0x5f, 0x5f));
        set(QPalette::Link, QColor(0x41, 0x9c, 0xff), QColor(0x41, 0x9c, 0xff));
        QApplication::setPalette(dark);
        QCoreApplication::processEvents();
        QVERIFY2(txs->configManager.systemUsesDarkMode(dark), "dark palette not recognised as dark mode");
        // The deprecated QGuiApplication::paletteChanged signal may not be
        // delivered here; run the same handler the system switch would.
        if (!darkMode) QVERIFY(QMetaObject::invokeMethod(txs, "paletteChanged", Q_ARG(QPalette, dark)));
        qInfo() << "demo dark mode" << darkMode;
        QVERIFY2(darkMode, "TeXstudio did not switch to dark mode");
        // paletteChanged installs a new format scheme, but the language
        // definitions (and so the highlighters) keep the one they were created
        // with. Load the dark formats into that scheme in place.
        QFormatScheme *scheme = txs->m_languages->defaultFormatScheme();
        QVERIFY(scheme);
        scheme->load(":/qxs/defaultFormatsDark.qxf");
        QDocument::setDefaultFormatScheme(scheme);
    }
    // Panels and the in-window menu bar (macOS shows menus outside the window).
    QList<QAction *> unchecked;
    for (const auto &id : spec["uncheckActions"].toArray())
        if (auto action = txs->getManagedAction(id.toString()); action && action->isChecked()) { action->setChecked(false); unchecked << action; }
    const auto restorePanels = qScopeGuard([&] { for (auto action : unchecked) action->setChecked(true); });
    if (spec.contains("toolbarIconSize")) {
        // As the preferences dialog does; the stored option alone did not resize the toolbars here.
        const int oldIcons = txs->configManager.guiToolbarIconSize;
        txs->changeIconSize(spec["toolbarIconSize"].toInt());
        qInfo() << "demo toolbar icon size" << oldIcons << "->" << txs->iconSize();
    }
    if (spec["hideMenuBar"].toBool() && txs->menuBar()) txs->menuBar()->hide();
    const auto restoreMenu = qScopeGuard([&] { if (txs->menuBar()) txs->menuBar()->show(); });

    // A frame is grabbed up to ~0.6 s after a key when the preview does not
    // change (cursor movement); without this the caret may be in its blink-off
    // phase. QEditor shows the caret after every key press, so a steady caret
    // matches what a person sees while typing or moving.
    const int oldFlash = QGuiApplication::styleHints()->cursorFlashTime();
    QGuiApplication::styleHints()->setCursorFlashTime(0);
    const auto restoreFlash = qScopeGuard([&] { QGuiApplication::styleHints()->setCursorFlashTime(oldFlash); });

    DemoPaintWatch watch;
    const auto ready = [&] { auto s = manager->current(); return s && s->preview.valid() && !s->failed(); };
    // Wait for at least one canvas paint after an edit, then for a quiet period.
    // Edits that do not change the output time out quietly into the next frame.
    auto settle = [&](int quietMs, int timeoutMs) {
        const qint64 start = watch.clock.elapsed();
        QTest::qWait(30);
        while (watch.clock.elapsed() - start < timeoutMs) {
            const qint64 now = watch.clock.elapsed();
            if (ready() && watch.last > start && now - watch.last >= quietMs) return true;
            if (ready() && watch.last <= start && now - start >= 2 * quietMs + 400) return true;
            QTest::qWait(10);
        }
        return false;
    };
    QJsonArray frames; int index = 0;
    auto capture = [&](int durationMs) {
        const auto name = QString("frame-%1.png").arg(index++, 4, 10, QChar('0'));
        const QPixmap pixmap = txs->grab();
        if (!pixmap.save(output + "/frames/" + name)) return false;
        frames.append(QJsonObject{{"file", name}, {"durationMs", durationMs},
            {"width", pixmap.width()}, {"height", pixmap.height()}, {"devicePixelRatio", pixmap.devicePixelRatio()}});
        return true;
    };
    auto view = [&](const QString &file) { return views.value(file); };
    auto editorOf = [&](const QJsonObject &a) {
        auto v = a.contains("file") ? view(a["file"].toString()) : qobject_cast<LatexEditorView *>(txs->currentEditorView());
        return v;
    };

    for (const auto &value : actions) {
        const auto a = value.toObject(); const auto op = a["op"].toString();
        if (op == "open") {
            auto v = txs->load(QDir(output).filePath(a["path"].toString())); QVERIFY2(v, qPrintable(a["path"].toString()));
            views.insert(a["path"].toString(), v);
        } else if (op == "closeOthers") {
            // Leave only the demo documents as tabs (the test fixture opens an empty one).
            for (auto other : txs->editors->editors()) {
                if (views.values().contains(other)) continue;
                txs->editors->setCurrentEditor(other); other->document->setClean(); txs->fileClose();
            }
        } else if (op == "show") {
            auto v = view(a["file"].toString()); QVERIFY(v);
            txs->editors->setCurrentEditor(v); v->editor->setFocus();
            const auto root = TexpressoSession::identity(v->document->getFileName());
            QTRY_VERIFY_WITH_TIMEOUT(manager->current() && manager->current()->root() == root && ready(), 60000);
            preview->viewerActions()["followCursor"]->setChecked(true);
            if (a.contains("continuous")) preview->viewerActions()["continuous"]->setChecked(a["continuous"].toBool());
            if (a.contains("zoom")) preview->viewerActions()[a["zoom"].toString()]->trigger();
            QVERIFY(settle(250, 15000));
        } else if (op == "find") {
            // Place the cursor at (or select) the first occurrence after `from`.
            auto v = editorOf(a); QVERIFY(v);
            const auto text = v->editor->text(); const auto needle = a["text"].toString();
            int at = text.indexOf(needle, a.contains("from") ? text.indexOf(a["from"].toString()) : 0);
            QVERIFY2(at >= 0, qPrintable(needle));
            const QString mode = a["mode"].toString("select");
            if (mode == "after") at += needle.size();
            // mode "at": cursor before the needle (initial placement only).
            const int line = text.left(at).count('\n'), column = at - text.lastIndexOf('\n', at - 1) - 1;
            QDocumentCursor cursor(v->editor->document(), line, column);
            if (mode == "select") cursor.movePosition(needle.size(), QDocumentCursor::NextCharacter, QDocumentCursor::KeepAnchor);
            v->editor->setCursor(cursor);
        } else if (op == "write") {
            auto v = editorOf(a); QVERIFY(v); v->editor->write(a["text"].toString());
        } else if (op == "move") {
            auto v = editorOf(a); QVERIFY(v); auto cursor = v->editor->cursor();
            // Arrow keys: characters or lines, optionally extending the selection (Shift).
            const int n = a["by"].toInt(); const bool lines = a["unit"].toString() == "line";
            const auto operation = lines ? (n < 0 ? QDocumentCursor::Up : QDocumentCursor::Down)
                                         : (n < 0 ? QDocumentCursor::PreviousCharacter : QDocumentCursor::NextCharacter);
            cursor.movePosition(qAbs(n), operation, a["select"].toBool() ? QDocumentCursor::KeepAnchor : QDocumentCursor::MoveAnchor);
            v->editor->setCursor(cursor);
        } else if (op == "type") {
            // Real key events into QEditor: auto-closing, completion and
            // overtyping behave exactly as for a person typing.
            auto v = editorOf(a); QVERIFY(v);
            for (const QChar c : a["text"].toString()) {
                if (c == '\n') QTest::keyClick(v->editor, Qt::Key_Return);
                else QTest::keyClicks(v->editor, QString(c));
            }
        } else if (op == "key") {
            static const QHash<QString, Qt::Key> keys{{"Left", Qt::Key_Left}, {"Right", Qt::Key_Right},
                {"Up", Qt::Key_Up}, {"Down", Qt::Key_Down}, {"Home", Qt::Key_Home}, {"End", Qt::Key_End},
                {"Return", Qt::Key_Return}, {"Tab", Qt::Key_Tab}, {"Escape", Qt::Key_Escape},
                {"Backspace", Qt::Key_Backspace}};
            auto v = editorOf(a); QVERIFY(v);
            QVERIFY2(keys.contains(a["key"].toString()), qPrintable(a["key"].toString()));
            QTest::keyClick(v->editor, keys[a["key"].toString()], a["shift"].toBool() ? Qt::ShiftModifier : Qt::NoModifier);
        } else if (op == "frame") {
            if (a["settle"].toBool(true)) settle(a["quietMs"].toInt(120), a["timeoutMs"].toInt(6000));
            QVERIFY(capture(a["durationMs"].toInt(80)));
        } else {
            QFAIL(qPrintable("Unknown demo action " + op));
        }
    }
    QSaveFile list(output + "/frames.json");
    QVERIFY(list.open(QIODevice::WriteOnly));
    list.write(QJsonDocument(QJsonObject{{"schema", 1}, {"platform", QGuiApplication::platformName()},
        {"frames", frames}}).toJson());
    QVERIFY(list.commit());
#endif
}
