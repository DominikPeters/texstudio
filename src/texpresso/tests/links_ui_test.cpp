#include <QtTest>
#include "texpressowidget.h"
#include <QDesktopServices>
#include <QFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QUrl>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>

class LinksUiTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void hoverAndCoordinates();
    void cursorArtworkAndModifiers();
    void cursorTracksStationaryHover();
    void activeToolsKeepTheirCursors();
    void linkClickPreservesTools();
    void modifiedClicksKeepExistingActions();
    void draggedAndRecycledLinksAreSuppressed();
    void realContinuousSecondaryPage();
public slots:
    void interceptUrl(const QUrl &url) { openedUrls.append(url); }
private:
    QList<QUrl> openedUrls;
};

static QJsonObject annotation(const QString &uri = "https://example.com/link")
{
    return {{"links", QJsonArray{QJsonObject{{"uri", uri}, {"bounds", QJsonArray{30, 40, 80, 60}}}}}};
}
static void compareCursor(const QCursor &actual, const QCursor &expected)
{
    QCOMPARE(actual.shape(), expected.shape());
    QCOMPARE(actual.hotSpot(), expected.hotSpot());
    QCOMPARE(actual.pixmap().toImage(), expected.pixmap().toImage());
}
void LinksUiTest::initTestCase()
{
    // Standalone tests do not link the editor's icon resources. Distinct
    // artwork verifies which shared cursor is selected, including hotspots.
    ViewerControls::initializeToolCursors([](const QString &name) {
        QPixmap pixmap(32, 32);
        const QHash<QString, QColor> colors{{"magnifier", Qt::red}, {"zoom-in-magnifier", Qt::green},
            {"zoom-out-magnifier", Qt::blue}, {"focusedhand", Qt::yellow}};
        pixmap.fill(colors.value(name));
        return pixmap;
    });
}
static void showCanvas(TexpressoCanvas &canvas)
{
    canvas.pixels = QImage(400, 200, QImage::Format_RGB32); canvas.pixels.fill(Qt::white);
    canvas.pageBounds = QRectF(10, 20, 200, 100); canvas.scale = 2; canvas.fresh = true;
    canvas.resize(400, 200); canvas.setAnnotations(7, 12, 3, annotation()); canvas.show();
    QCoreApplication::processEvents();
}
void LinksUiTest::hoverAndCoordinates()
{
    PDFDocumentConfig config{}; config.editTool = 1;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    QTest::mouseMove(&canvas, {60, 50});
    QCOMPARE(canvas.cursor().shape(), Qt::PointingHandCursor); QCOMPARE(canvas.toolTip(), QString("https://example.com/link"));
    QTest::mouseMove(&canvas, {10, 10}); compareCursor(canvas.cursor(), ViewerControls::toolCursor(1, Qt::NoModifier)); QVERIFY(canvas.toolTip().isEmpty());
    for (const auto &uri : {"javascript:alert(1)", "file:///tmp/private", "#"}) {
        canvas.setAnnotations(7, 12, 3, annotation(uri)); QTest::mouseMove(&canvas, {60, 50});
        compareCursor(canvas.cursor(), ViewerControls::toolCursor(1, Qt::NoModifier)); QVERIFY(canvas.toolTip().isEmpty());
        QTest::mouseMove(&canvas, {10, 10});
    }
    canvas.setAnnotations(7, 12, 3, annotation("#later")); QTest::mouseMove(&canvas, {60, 50});
    QCOMPARE(canvas.cursor().shape(), Qt::PointingHandCursor);
    canvas.fresh = false; canvas.resetTools(); QTest::mouseMove(&canvas, {61, 50});
    compareCursor(canvas.cursor(), ViewerControls::toolCursor(1, Qt::NoModifier)); QVERIFY(canvas.toolTip().isEmpty());
}
void LinksUiTest::cursorArtworkAndModifiers()
{
    PDFDocumentConfig config{}; config.editTool = 1;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    const QPoint point(10, 10);
    const auto move = [&](Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseMove, point, canvas.mapToGlobal(point), Qt::NoButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(&canvas, &event);
    };
    move(Qt::NoModifier);
    QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
    QCOMPARE(canvas.cursor().hotSpot(), QPoint(10, 10));
    QCOMPARE(canvas.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::red));
    move(Qt::ShiftModifier);
    QCOMPARE(canvas.cursor().hotSpot(), QPoint(12, 12));
    QCOMPARE(canvas.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::green));
    move(Qt::AltModifier);
    QCOMPARE(canvas.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::blue));
    move(Qt::ShiftModifier | Qt::ControlModifier);
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
    canvas.setTool(2); move(Qt::NoModifier);
    QCOMPARE(canvas.cursor().hotSpot(), QPoint(8, 13));
    QCOMPARE(canvas.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::yellow));
    move(Qt::ShiftModifier); QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    move(Qt::ShiftModifier | Qt::ControlModifier); QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
}
void LinksUiTest::cursorTracksStationaryHover()
{
    PDFDocumentConfig config{}; config.editTool = 1;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    QCursor::setPos(canvas.mapToGlobal(QPoint(60, 50)));
    QEvent enter(QEvent::Enter); QCoreApplication::sendEvent(&canvas, &enter);
    QCOMPARE(canvas.cursor().shape(), Qt::PointingHandCursor);
    QTest::keyPress(&canvas, Qt::Key_Shift);
    // Match the PDF viewer's link hover, while modified clicks remain tools.
    QCOMPARE(canvas.cursor().shape(), Qt::PointingHandCursor);
    QTest::keyRelease(&canvas, Qt::Key_Shift);
    QCursor::setPos(canvas.mapToGlobal(QPoint(10, 10)));
    QTest::keyPress(&canvas, Qt::Key_Shift);
    compareCursor(canvas.cursor(), ViewerControls::toolCursor(1, Qt::ShiftModifier));
    QVERIFY(canvas.toolTip().isEmpty());
    QTest::keyRelease(&canvas, Qt::Key_Shift);
    compareCursor(canvas.cursor(), ViewerControls::toolCursor(1, Qt::NoModifier));
    QCursor::setPos(canvas.mapToGlobal(QPoint(60, 50)));
    QFocusEvent focus(QEvent::FocusIn); QCoreApplication::sendEvent(&canvas, &focus);
    QCOMPARE(canvas.cursor().shape(), Qt::PointingHandCursor);
    QEvent leave(QEvent::Leave); QCoreApplication::sendEvent(&canvas, &leave);
    compareCursor(canvas.cursor(), ViewerControls::toolCursor(1, Qt::NoModifier));
    QVERIFY(canvas.toolTip().isEmpty());
}
void LinksUiTest::activeToolsKeepTheirCursors()
{
    PDFDocumentConfig config{}; config.editTool = 1; config.magnifierSize = 120;
    config.magnifierShape = PDFDocumentConfig::Circle;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {10, 10});
    QVERIFY(canvas.findChild<PDFMagnifier *>()->isVisible());
    QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
    QTest::mouseMove(&canvas, {60, 50});
    QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor); QVERIFY(canvas.toolTip().isEmpty());
    QKeyEvent shift(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &shift);
    QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
    QCOMPARE(canvas.cursor().shape(), Qt::PointingHandCursor);
    QVERIFY(!canvas.findChild<PDFMagnifier *>()->isVisible());
    canvas.setTool(2);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {10, 10});
    QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
    QTest::mouseMove(&canvas, {60, 50});
    QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
    QCoreApplication::sendEvent(&canvas, &shift);
    QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {10, 10});
    compareCursor(canvas.cursor(), ViewerControls::toolCursor(2, Qt::NoModifier));
    QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, {10, 10});
    QVERIFY(canvas.interactionActive);
    QVERIFY(canvas.findChild<PDFMagnifier *>()->isVisible());
    QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
    QTest::mouseMove(&canvas, {60, 50});
    QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {10, 10});
    QVERIFY(!canvas.interactionActive);
    compareCursor(canvas.cursor(), ViewerControls::toolCursor(2, Qt::NoModifier));
}
void LinksUiTest::linkClickPreservesTools()
{
    PDFDocumentConfig config{}; config.editTool = 1;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    QSignalSpy links(&canvas, &TexpressoCanvas::linkActivated), source(&canvas, &TexpressoCanvas::sourceClicked);
    for (int tool : {1, 2}) {
        canvas.setTool(tool); QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
        QVERIFY(!canvas.findChild<PDFMagnifier *>()->isVisible());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
    }
    QCOMPARE(links.size(), 2); QVERIFY(source.isEmpty());
    const auto values = links.last(); QCOMPARE(values[0].toInt(), 7); QCOMPARE(values[1].toInt(), 0);
    QCOMPARE(values[2].toLongLong(), 12); QCOMPARE(values[3].toLongLong(), 3);
}
void LinksUiTest::modifiedClicksKeepExistingActions()
{
    PDFDocumentConfig config{}; config.editTool = 1;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    QSignalSpy links(&canvas, &TexpressoCanvas::linkActivated), source(&canvas, &TexpressoCanvas::sourceClicked), zoom(&canvas, &TexpressoCanvas::zoomRequested);
    for (const auto modifier : {Qt::ControlModifier, Qt::MetaModifier})
        QTest::mouseClick(&canvas, Qt::LeftButton, modifier, {60, 50});
    QCOMPARE(source.size(), 2); QCOMPARE(source.last()[0].toPointF(), QPointF(40, 45)); QVERIFY(links.isEmpty());
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ShiftModifier, {60, 50});
    QCOMPARE(zoom.size(), 1); QCOMPARE(zoom.last()[0].toInt(), 1);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::AltModifier, {60, 50});
    QCOMPARE(zoom.size(), 2); QCOMPARE(zoom.last()[0].toInt(), -1); QVERIFY(links.isEmpty());
}
void LinksUiTest::draggedAndRecycledLinksAreSuppressed()
{
    PDFDocumentConfig config{}; config.editTool = 2;
    QScrollArea area; TexpressoCanvas canvas(&config, &area); showCanvas(canvas);
    QSignalSpy links(&canvas, &TexpressoCanvas::linkActivated), zoom(&canvas, &TexpressoCanvas::zoomRequested);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
    QTest::mouseMove(&canvas, {180, 50}); QTest::mouseMove(&canvas, {60, 50});
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50}); QVERIFY(links.isEmpty());
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
    canvas.setAnnotations(8, 12, 3, annotation());
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50}); QVERIFY(links.isEmpty());
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
    canvas.setAnnotations(8, 13, 4, annotation());
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::ShiftModifier, {60, 50}); QVERIFY(links.isEmpty()); QVERIFY(zoom.isEmpty());
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50});
    canvas.fresh = false; canvas.resetTools();
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {60, 50}); QVERIFY(links.isEmpty());
}
void LinksUiTest::realContinuousSecondaryPage()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for hyperlink viewer integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\\documentclass{article}\n\\usepackage[xetex,hidelinks]{hyperref}\n\\begin{document}\n"
        "\\hypertarget{first}{First page.}\\newpage\n"
        "\\href{https://example.com/secondary}{External link on second page.}\\par\n"
        "\\hyperlink{first}{Back to first page.}\\end{document}\n"); file.close();
    TexpressoManager manager(nullptr, engine);
    PDFDocumentConfig config{}; config.editTool = 2; config.zoomStepFactor = std::sqrt(2.0);
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 1450); widget.show(); manager.activate(root);
    const auto session = manager.current(); session->setAutoRerun(false); session->followCursor = false;
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid(), 15000);
    auto slider = widget.findChild<QSlider *>(); QVERIFY(slider); slider->setValue(-100);
    QPointer<TexpressoCanvas> secondary;
    const auto findSecondary = [&] {
        for (auto item : widget.findChildren<TexpressoCanvas *>("texpressoNeighborPage"))
            if (item->isVisible() && item->fresh && item->property("texpressoPage").toInt() == 1) { secondary = item; return true; }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findSecondary(), 20000);
    const auto annotations = session->preview.annotationsForPage(1).value("links").toArray();
    int external = -1, internal = -1;
    for (int i = 0; i < annotations.size(); ++i) {
        const auto uri = annotations[i].toObject().value("uri").toString();
        if (uri == "https://example.com/secondary") external = i;
        if (uri == "#first") internal = i;
    }
    QVERIFY(external >= 0); QVERIFY(internal >= 0);
    const auto bounds = annotations[external].toObject().value("bounds").toArray();
    const QPointF center((bounds[0].toDouble() + bounds[2].toDouble()) / 2, (bounds[1].toDouble() + bounds[3].toDouble()) / 2);
    const QPoint point = ((center - secondary->pageBounds.topLeft()) * secondary->scale).toPoint();
    openedUrls.clear(); QDesktopServices::setUrlHandler("https", this, "interceptUrl");
    const auto urlHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); });
    QTest::mouseClick(secondary, Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(openedUrls, QList<QUrl>{QUrl("https://example.com/secondary")});
    QSignalSpy sources(&manager, &TexpressoManager::sourceLocation);
    QTest::mouseClick(secondary, Qt::LeftButton, Qt::ControlModifier, point);
    QTRY_VERIFY_WITH_TIMEOUT(!sources.isEmpty(), 15000);
    QCOMPARE(QFileInfo(sources.last()[0].toString()).canonicalFilePath(), QFileInfo(root).canonicalFilePath());
    QCOMPARE(openedUrls.size(), 1);
    // Source lookup keeps the clicked page active while it remains visible.
    // Restore the first page so the next link still exercises a neighbor
    // canvas independently of the stale active-canvas geometry below.
    session->preview.navigateTo(0);
    QTRY_VERIFY_WITH_TIMEOUT(findSecondary(), 15000);
    auto activeCanvas = widget.findChild<TexpressoCanvas *>("texpressoActivePage"); QVERIFY(activeCanvas);
    bool geometryReadyAtReveal = false;
    connect(&session->preview, &TexpressoPreview::reveal, &widget, [&](const QPointF &) {
        geometryReadyAtReveal = activeCanvas->pageBounds == session->preview.bounds();
    });
    const auto internalBounds = annotations[internal].toObject().value("bounds").toArray();
    const QPointF internalCenter((internalBounds[0].toDouble() + internalBounds[2].toDouble()) / 2,
        (internalBounds[1].toDouble() + internalBounds[3].toDouble()) / 2);
    // Simulate geometry retained from a previous page with a different size
    // before the queued widget refresh applies the destination frame. The
    // secondary canvas used to click the link keeps its correct hit geometry.
    activeCanvas->pageBounds = QRectF(20, 30, 200, 300);
    QSignalSpy internalClicks(secondary, &TexpressoCanvas::linkActivated);
    QTest::mouseClick(secondary, Qt::LeftButton, Qt::NoModifier,
        ((internalCenter - secondary->pageBounds.topLeft()) * secondary->scale).toPoint());
    QCOMPARE(internalClicks.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.page() == 0 && session->preview.valid() && geometryReadyAtReveal, 15000);
    const auto destinations = session->preview.annotationsForPage(0).value("destinations").toArray();
    const auto destination = std::find_if(destinations.begin(), destinations.end(), [](const QJsonValue &item) {
        return item.toObject().value("name").toString() == "first";
    });
    QVERIFY(destination != destinations.end());
    const auto anchor = destination->toObject();
    auto area = widget.findChild<QScrollArea *>(); QVERIFY(area);
    const QPoint localAnchor = ((QPointF(anchor["x"].toDouble(), anchor["y"].toDouble())
        - activeCanvas->pageBounds.topLeft()) * activeCanvas->scale).toPoint();
    const int documentY = activeCanvas->pos().y() + localAnchor.y();
    const int expectedScroll = std::clamp(documentY - 12, area->verticalScrollBar()->minimum(), area->verticalScrollBar()->maximum());
    QCOMPARE(area->verticalScrollBar()->value(), expectedScroll);
    QCOMPARE(openedUrls.size(), 1);
    manager.shutdown();
}
QTEST_MAIN(LinksUiTest)
#include "links_ui_test.moc"
