#include <QtTest>
#include "ViewerPanel.h"
#include "texpressomanager.h"
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>

class ViewerPanelTest : public QObject
{
    Q_OBJECT
private slots:
    void projectModesAndCachedViews();
    void visibleShortcutsAndFocus();
    void closingAndVisibility();
    void modeSwitchLeadsViewerToolbars();
    void hiddenPreviewKeepsCompilerWarm();
};
void ViewerPanelTest::projectModesAndCachedViews()
{
    QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    ViewerPanel panel(&settings); auto live = new QLineEdit; panel.setLiveViewer(live);
    auto pdfA = new QLineEdit("PDF A"), pdfB = new QLineEdit("PDF B");
    panel.addPdf("a", pdfA); panel.setProject("a"); panel.show(); QCoreApplication::processEvents();
    QVERIFY(panel.liveVisible());
    QSignalSpy requests(&panel, &ViewerPanel::pdfRequested);
    panel.setMode(ViewerPanel::Pdf); QCOMPARE(panel.currentViewer(), pdfA); QVERIFY(!panel.liveVisible());
    pdfA->setCursorPosition(3);
    panel.setProject("b"); QCOMPARE(panel.mode(), ViewerPanel::Live);
    panel.setMode(ViewerPanel::Pdf); QVERIFY(panel.currentViewer() != pdfA);
    QCOMPARE(requests.size(), 1); QCOMPARE(requests.last()[0].toString(), QString("b"));
    panel.addPdf("b", pdfB); QCOMPARE(panel.currentViewer(), pdfB);
    panel.setProject("a"); QCOMPARE(panel.mode(), ViewerPanel::Pdf); QCOMPARE(panel.currentViewer(), pdfA);
    QCOMPARE(pdfA->cursorPosition(), 3); QVERIFY(!pdfB->isVisible());
    panel.setMode(ViewerPanel::Live); panel.setProject("b"); QCOMPARE(panel.mode(), ViewerPanel::Pdf);
    settings.sync();
    QSettings restoredSettings(settings.fileName(), QSettings::IniFormat);
    ViewerPanel restored(&restoredSettings); restored.setProject("a"); QCOMPARE(restored.mode(), ViewerPanel::Live);
    restored.setProject("b"); QCOMPARE(restored.mode(), ViewerPanel::Pdf);
    restored.setPdfSupported(false); QCOMPARE(restored.mode(), ViewerPanel::Live);
}
void ViewerPanelTest::visibleShortcutsAndFocus()
{
    QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    ViewerPanel panel(&settings); panel.resize(600, 400);
    auto live = new QLineEdit, pdf = new QLineEdit;
    panel.setLiveViewer(live); panel.addPdf("a", pdf); panel.setProject("a");
    const auto action = [](QWidget *widget) {
        auto a = new QAction(widget); a->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut); widget->addAction(a); return a;
    };
    QSignalSpy liveShortcuts(action(live), &QAction::triggered), pdfShortcuts(action(pdf), &QAction::triggered);
    panel.show(); panel.activateWindow(); live->setFocus(); QTRY_VERIFY(live->hasFocus());
    QTest::keyClick(live, Qt::Key_L, Qt::ControlModifier);
    QCOMPARE(liveShortcuts.size(), 1); QVERIFY(pdfShortcuts.isEmpty());
    panel.setMode(ViewerPanel::Pdf); QTRY_VERIFY(pdf->hasFocus()); QVERIFY(!live->isVisible());
    QTest::keyClick(pdf, Qt::Key_L, Qt::ControlModifier);
    QCOMPARE(pdfShortcuts.size(), 1); QCOMPARE(liveShortcuts.size(), 1);
    panel.setMode(ViewerPanel::Live); QTRY_VERIFY(live->hasFocus());
}
void ViewerPanelTest::closingAndVisibility()
{
    QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    ViewerPanel panel(&settings); panel.setLiveViewer(new QWidget); panel.setProject("a");
    QSignalSpy visibility(&panel, &ViewerPanel::liveVisibilityChanged);
    panel.show(); QCoreApplication::processEvents(); QVERIFY(panel.liveVisible());
    panel.hide(); QVERIFY(!panel.liveVisible()); panel.show(); QVERIFY(panel.liveVisible());
    auto pdf = new QWidget; panel.addPdf("a", pdf); panel.setMode(ViewerPanel::Pdf);
    QVERIFY(!panel.liveVisible()); QCOMPARE(panel.currentViewer(), pdf);
    delete pdf; QCoreApplication::processEvents(); QVERIFY(!panel.pdf("a"));
    QVERIFY(panel.currentViewer()); QCOMPARE(panel.mode(), ViewerPanel::Pdf);
    panel.setMode(ViewerPanel::Live); QVERIFY(panel.liveVisible());
    QCOMPARE(visibility.size(), 5);
    auto replaced = new ViewerPanel(&settings);
    QPointer<QWidget> oldPdf = new QWidget, newPdf = new QWidget;
    replaced->addPdf("a", oldPdf); replaced->addPdf("a", newPdf);
    delete replaced; QVERIFY(oldPdf.isNull()); QVERIFY(newPdf.isNull());
}
void ViewerPanelTest::modeSwitchLeadsViewerToolbars()
{
    QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    ViewerPanel panel(&settings); panel.resize(600, 400);
    QVERIFY(!panel.findChild<QLabel *>(QString(), Qt::FindDirectChildrenOnly));
    const auto viewer = [](QAction *&existing) {
        auto widget = new QWidget; auto toolbar = new QToolBar(widget);
        existing = toolbar->addAction("Zoom"); return widget;
    };
    QAction *liveZoom, *pdfZoom;
    auto live = viewer(liveZoom), pdf = viewer(pdfZoom);
    panel.setLiveViewer(live); panel.setProject("a");
    const auto modeActions = [](QWidget *widget) { return widget->findChild<QToolBar *>()->actions(); };
    auto actions = modeActions(live);
    QCOMPARE(actions.size(), 4); QCOMPARE(actions[0]->objectName(), QString("previewModeLive"));
    QCOMPARE(actions[1]->objectName(), QString("previewModePdf")); QVERIFY(actions[2]->isSeparator());
    QCOMPARE(actions[3], liveZoom); QVERIFY(actions[0]->isChecked());
    // The placeholder shown before a PDF exists keeps a way back to Live.
    actions[1]->trigger(); QCOMPARE(panel.mode(), ViewerPanel::Pdf);
    auto placeholder = panel.currentViewer(); QVERIFY(placeholder != live);
    QVERIFY(modeActions(placeholder).contains(actions[0]));
    panel.addPdf("a", pdf); panel.addPdf("a", pdf);
    QCOMPARE(modeActions(pdf).size(), 4); QCOMPARE(modeActions(pdf)[3], pdfZoom);
    QCOMPARE(panel.currentViewer(), pdf); QVERIFY(actions[1]->isChecked());
    actions[0]->trigger(); QCOMPARE(panel.mode(), ViewerPanel::Live); QCOMPARE(panel.currentViewer(), live);
    panel.setPdfSupported(false); QVERIFY(!actions[0]->isVisible()); QVERIFY(!actions[1]->isVisible());
}
void ViewerPanelTest::hiddenPreviewKeepsCompilerWarm()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for compiler/rendering visibility integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QString source = "\\documentclass{article}\n\\begin{document}\nBefore.\\typeout{VISIBLEBEFORE}\\end{document}\n";
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(source.toUtf8()); file.close();
    TexpressoManager manager(nullptr, engine); manager.openBuffer(root, source); manager.activate(root);
    auto session = manager.current(); session->followCursor = false; session->setAutoRerun(false);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && !session->preview.busy(), 15000);
    const auto pid = session->processId();
    const auto before = session->preview.image();
    manager.setPreviewVisible(false); QTest::qWait(100);
    QSignalSpy commands(&session->preview, &TexpressoPreview::command);
    QSignalSpy preparation(&session->prewarm, &TexpressoPrewarm::command);
    session->prepareEdit(root, 2, 0); QTest::qWait(250); QVERIFY(preparation.isEmpty());
    source.replace("Before.", "After.").replace("VISIBLEBEFORE", "HIDDENAFTER");
    manager.reconcileBuffer(root, source); session->prepareEdit(root, 2, 0);
    QTRY_VERIFY_WITH_TIMEOUT(session->compilerOutput.contains("HIDDENAFTER"), 15000);
    QTest::qWait(200);
    QVERIFY(preparation.isEmpty());
    QVERIFY(session->active()); QVERIFY(!session->userPaused); QCOMPARE(session->processId(), pid);
    for (const auto &row : commands) {
        const auto verb = row[0].toJsonArray()[0].toString();
        QVERIFY2(verb != "preview-render" && verb != "preview-png" && verb != "preview-prepare", qPrintable(verb));
    }
    manager.setPreviewVisible(true);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid() && !session->preview.busy() && session->preview.image() != before, 15000);
    QCOMPARE(session->processId(), pid);
}
QTEST_MAIN(ViewerPanelTest)
#include "viewer_panel_test.moc"
