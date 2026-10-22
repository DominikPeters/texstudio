#include <QtTest>
#include "texpressoprotocol.h"
#include "texpressomanager.h"
#include "texpressowidget.h"
#include "ViewerControls.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QBuffer>
#include <csignal>
#include <cmath>

class ClientTest : public QObject
{
    Q_OBJECT
private slots:
    void fragmentedBinary();
    void truncatedBinary();
    void invalidDimensions();
    void obsoleteDecode();
    void emptyPatchFrame();
    void realProjects();
    void realProjects_data();
    void viewerControls();
    void viewerInteractions();
    void magnifierPixels();
    void pageCache();
    void cacheEviction_data();
    void cacheEviction();
    void canvasColors();
};
static QJsonObject header(int bytes) {
    return {{"requestId", 1}, {"revision", 2}, {"sequence", 3}, {"page", 0},
        {"bounds", QJsonArray{0, 0, 612, 792}}, {"width", 1200}, {"height", 1553},
        {"mime", "image/png"}, {"byteLength", bytes}};
}
void ClientTest::fragmentedBinary()
{
    TexpressoProtocol protocol;
    QSignalSpy images(&protocol, &TexpressoProtocol::image), messages(&protocol, &TexpressoProtocol::message), errors(&protocol, &TexpressoProtocol::failed);
    const QByteArray binary("\0\n\xff[\"flush\"]\n", 14);
    const QByteArray stream = QJsonDocument(QJsonArray{"preview-image", header(binary.size())}).toJson(QJsonDocument::Compact)
        + '\n' + binary + "[\"flush\"]\n";
    for (char byte : stream) protocol.feed(QByteArray(1, byte));
    protocol.finish();
    QCOMPARE(errors.size(), 0); QCOMPARE(images.size(), 1); QCOMPARE(messages.size(), 1);
    QCOMPARE(images[0][1].toByteArray(), binary);
}
void ClientTest::truncatedBinary()
{
    TexpressoProtocol protocol; QSignalSpy errors(&protocol, &TexpressoProtocol::failed);
    protocol.feed(QJsonDocument(QJsonArray{"preview-image", header(100)}).toJson(QJsonDocument::Compact) + "\nshort");
    protocol.finish(); QCOMPARE(errors.size(), 1);
    protocol.feed("[\"flush\"]\n"); QCOMPARE(errors.size(), 1);
}
void ClientTest::invalidDimensions()
{
    auto h = header(100); h["width"] = 8192; h["height"] = 8192;
    QVERIFY(!TexpressoProtocol::validImage(h));
    h = header(100); h["width"] = -1; QVERIFY(!TexpressoProtocol::validImage(h));
    h = header(0); QVERIFY(!TexpressoProtocol::validImage(h));
}
void ClientTest::emptyPatchFrame()
{
    TexpressoProtocol protocol;
    QSignalSpy images(&protocol, &TexpressoProtocol::image), messages(&protocol, &TexpressoProtocol::message);
    auto h = header(0); h["rasterId"] = 2;
    h["patch"] = QJsonObject{{"base", 1}, {"rect", QJsonArray{0, 0, 0, 0}}};
    protocol.feed(QJsonDocument(QJsonArray{"preview-image", h}).toJson(QJsonDocument::Compact) + "\n[\"flush\"]\n");
    QCOMPARE(images.size(), 1); QVERIFY(images[0][1].toByteArray().isEmpty()); QCOMPARE(messages.size(), 1);
}
void ClientTest::obsoleteDecode()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.sourceChanged(3);
    preview.message({"preview-state", 2, 3, 1, true, 0});
    QImage red(2, 2, QImage::Format_RGB32); red.fill(Qt::red);
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); QVERIFY(red.save(&output, "PNG"));
    auto h = header(bytes.size()); h["width"] = 2; h["height"] = 2;
    preview.receive(h, bytes); // Decoding happens on another thread.
    preview.sourceChanged(4);
    QTest::qWait(50); QVERIFY(!preview.valid()); QVERIFY(preview.image().isNull());
    preview.message({"preview-state", 3, 4, 1, true, 0});
    h["requestId"] = commands.last()[0].toJsonArray()[1]; h["revision"] = 3; h["sequence"] = 4;
    preview.receive(h, bytes); QTRY_VERIFY(preview.valid()); QCOMPARE(preview.image(), red);
}
static QJsonObject imageFrame(TexpressoPreview &preview, const QJsonArray &request, const QImage &image)
{
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); image.save(&output, "PNG");
    auto h = header(bytes.size()); h["requestId"] = request[1]; h["page"] = request[2];
    h["width"] = image.width(); h["height"] = image.height();
    h["revision"] = double(preview.revision()); h["sequence"] = double(preview.sequence());
    preview.receive(h, bytes); return h;
}
void ClientTest::pageCache()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.sourceChanged(3); preview.message({"preview-state", 2, 3, 2, true, 0});
    QImage red(2, 2, QImage::Format_RGB32); red.fill(Qt::red);
    imageFrame(preview, commands.last()[0].toJsonArray(), red); QTRY_VERIFY(preview.valid());
    preview.setPage(1); QImage blue = red; blue.fill(Qt::blue);
    imageFrame(preview, commands.last()[0].toJsonArray(), blue); QTRY_VERIFY(preview.valid());
    const int count = commands.size(); preview.setPage(0);
    QVERIFY(preview.valid()); QCOMPARE(preview.image(), red); QCOMPARE(commands.size(), count);
    // Cached pixels cannot be used with the engine's other-page display list.
    preview.inverse({20, 30}); QCOMPARE(commands.size(), count + 1);
    auto request = commands.last()[0].toJsonArray(); QCOMPARE(request[0].toString(), QString("preview-render"));
    imageFrame(preview, request, red);
    QTRY_COMPARE(commands.last()[0].toJsonArray()[0].toString(), QString("preview-inverse"));
    QCOMPARE(commands.last()[0].toJsonArray()[3].toInt(), 0);
    // An inverse click abandoned by another page change must never be sent.
    preview.setPage(1); preview.inverse({40, 50}); request = commands.last()[0].toJsonArray();
    preview.setPage(0); const int before = commands.size(); imageFrame(preview, request, blue);
    QTest::qWait(30); QCOMPARE(commands.size(), before);
    preview.sourceChanged(4); QVERIFY(!preview.valid()); preview.message({"preview-state", 3, 4, 2, true, 0});
    QCOMPARE(commands.last()[0].toJsonArray()[0].toString(), QString("preview-render"));
    QVERIFY(!preview.valid()); // no reuse across source revisions
    imageFrame(preview, commands.last()[0].toJsonArray(), blue); QTRY_VERIFY(preview.valid());
    preview.setWidth(600);
    imageFrame(preview, commands.last()[0].toJsonArray(), red);
    QTRY_COMPARE(preview.image(), red);
    const int resized = commands.size(); preview.setWidth(1200);
    QVERIFY(preview.valid()); QCOMPARE(preview.image(), blue); QCOMPARE(commands.size(), resized);
    preview.message({"preview-invalidated", 4, 4}); QVERIFY(!preview.valid());
    QCOMPARE(commands.last()[0].toJsonArray()[0].toString(), QString("preview-render"));
}
void ClientTest::cacheEviction_data()
{
    QTest::addColumn<QSize>("size"); QTest::addColumn<int>("count");
    QTest::newRow("entry-budget") << QSize(2, 2) << 5;
    QTest::newRow("byte-budget") << QSize(4000, 1000) << 3;
}
void ClientTest::cacheEviction()
{
    QFETCH(QSize, size); QFETCH(int, count);
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, count, true, 0});
    QImage image(size, QImage::Format_RGB32); image.fill(Qt::red);
    for (int i = 0; i < count; ++i) {
        preview.setPage(i); imageFrame(preview, commands.last()[0].toJsonArray(), image); QTRY_VERIFY(preview.valid());
    }
    const int before = commands.size(); preview.setPage(0);
    QVERIFY(!preview.valid()); QCOMPARE(commands.size(), before + 1);
}
void ClientTest::canvasColors()
{
    PDFDocumentConfig config{}; config.editTool = 2;
    QScrollArea area; auto canvas = new TexpressoCanvas(&config, &area); area.setWidget(canvas);
    canvas->resize(20, 20); canvas->pixels = QImage(2, 2, QImage::Format_RGB32); canvas->pixels.fill(Qt::red);
    QImage target(20, 20, QImage::Format_RGB32);
    const auto check = [&] {
        canvas->render(&target);
        const auto reference = convertImage(QPixmap::fromImage(canvas->pixels), config.invertColors, config.grayscale).toImage();
        QCOMPARE(target.pixelColor(10, 10), reference.pixelColor(0, 0));
    };
    check(); config.grayscale = true; check(); check();
    config.invertColors = true; check();
    canvas->pixels.fill(Qt::blue); check();
    config.grayscale = false; check(); config.invertColors = false; check();
}
static void write(const QString &path, const QString &text) {
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(text.toUtf8());
}
void ClientTest::viewerControls()
{
    // Preserve the PDF viewer's 100% slider detent and asymmetric range.
    QCOMPARE(ViewerControls::sliderToScale(-100), .125);
    QCOMPARE(ViewerControls::sliderToScale(100), 4.0);
    for (int position = -100; position <= 100; ++position) {
        const auto scale = ViewerControls::sliderToScale(position);
        if (std::abs(position) <= 10) QCOMPARE(scale, 1.0);
        else if (std::abs(scale - 1) < .01) QCOMPARE(ViewerControls::scaleToSlider(scale), 0);
        else QCOMPARE(ViewerControls::scaleToSlider(scale), position);
    }
    QVERIFY(ViewerControls::isSyncClick(Qt::ControlModifier));
    QVERIFY(!ViewerControls::isSyncClick(Qt::ControlModifier | Qt::ShiftModifier));
    QVERIFY(!ViewerControls::isSyncClick(Qt::NoModifier));
}
void ClientTest::magnifierPixels()
{
    PDFDocumentConfig config{};
    config.editTool = 1; config.magnifierSize = 160;
    config.magnifierShape = PDFDocumentConfig::Circle; config.magnifierBorder = true;
    QScrollArea area; area.resize(4200, 2200);
    auto canvas = new TexpressoCanvas(&config, &area);
    canvas->pixels = QImage(8, 8, QImage::Format_RGB32); canvas->pixels.fill(Qt::red);
    canvas->pageBounds = QRectF(0, 0, 4200, 2200); canvas->resize(4200, 2200); canvas->fresh = true;
    area.setWidget(canvas); area.show(); QCoreApplication::processEvents();
    const QPoint point(2000, 1000);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point);
    auto lens = canvas->findChild<PDFMagnifier *>(); QVERIFY(lens && lens->isVisible());
    const auto image = lens->grab().toImage();
    QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(Qt::red));
    QTest::mouseMove(canvas, point + QPoint(200, 100));
    const auto moved = lens->grab().toImage();
    QCOMPARE(moved.pixelColor(moved.width() / 2, moved.height() / 2), QColor(Qt::red));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point);
    canvas->pixels.fill(Qt::blue); canvas->invalidateMagnifier();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point);
    const auto changed = lens->grab().toImage();
    QCOMPARE(changed.pixelColor(changed.width() / 2, changed.height() / 2), QColor(Qt::blue));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point);
}
void ClientTest::viewerInteractions()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE to run viewer integration tests");
    QTemporaryDir project;
    const auto root = project.filePath("main.tex");
    write(root, "\\documentclass{article}\n\\begin{document}\nFirst page\\newpage Second page\n\\end{document}\n");
    TexpressoManager manager(nullptr, engine);
    PDFDocumentConfig config{};
    config.editTool = 1; config.zoomStepFactor = std::sqrt(2.0);
    config.magnifierSize = 160; config.magnifierShape = PDFDocumentConfig::Circle;
    config.magnifierBorder = true;
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(640, 700); widget.show();
    manager.activate(root);
    auto session = manager.current();
    QTRY_VERIFY_WITH_TIMEOUT(session->preview.valid(), 15000);
    auto canvas = widget.findChild<TexpressoCanvas *>(); QVERIFY(canvas);
    const auto actions = widget.viewerActions();
    QVERIFY(!actions.contains("selectText"));
    QVERIFY(!actions["previous"]->isEnabled());
    QVERIFY(actions["next"]->isEnabled());
    actions["next"]->trigger();
    QTRY_VERIFY(session->preview.valid() && session->preview.page() == 1);
    QTRY_VERIFY(actions["previous"]->isEnabled());
    actions["actualSize"]->trigger(); QCOMPARE(session->preview.zoom, 1.0);
    actions["zoomIn"]->trigger(); QVERIFY(session->preview.zoom > 1.0);
    actions["fitToWindow"]->trigger(); QCOMPARE(session->preview.zoom, -1.0);
    QTRY_VERIFY(session->preview.valid());
    QSignalSpy source(canvas, &TexpressoCanvas::sourceClicked);
    const QPoint point(100, 120);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::ControlModifier, point);
    QCOMPARE(source.size(), 1);
    const auto expected = QPointF(point) / canvas->scale + canvas->pageBounds.topLeft();
    QCOMPARE(source[0][0].toPointF(), expected);
    // The actual PDF magnifier is also the live viewer's lens.
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point);
    auto lens = canvas->findChild<PDFMagnifier *>(); QVERIFY(lens && lens->isVisible());
    QVERIFY(!lens->grab().isNull());
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point);
    QVERIFY(!lens->isVisible());
    canvas->fresh = false;
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::ControlModifier, point);
    QCOMPARE(source.size(), 1); // stale frames cannot navigate the source
    manager.shutdown();
}
void ClientTest::realProjects_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("explicit-extension") << QString("child.tex");
    QTest::newRow("extensionless") << QString("child");
}
void ClientTest::realProjects()
{
    QFETCH(QString, input);
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE to run real bundled-engine tests");
    QTemporaryDir temp; QVERIFY(temp.isValid());
    QDir dir(temp.path()); QVERIFY(dir.mkdir("one")); QVERIFY(dir.mkdir("two"));
    const auto one = dir.filePath("one/main.tex"), two = dir.filePath("two/main.tex"), child = dir.filePath("one/child.tex");
    const QString root = "\\documentclass{article}\n\\begin{document}\n\\input{" + input + "}\n\\end{document}\n";
    write(one, root); write(child, "Disk contents\n");
    write(two, "\\documentclass{article}\n\\begin{document}\nSecond project\n\\newpage\nAnother page\n\\end{document}\n");
    TexpressoManager manager(nullptr, engine);
    const auto prefix = QString::fromUtf8("Unsaved café 🐾");
    const QString suffix = " text\n\\typeout{OVERLAY-INCLUDED}\n";
    manager.openBuffer(one, root); manager.openBuffer(child, prefix + suffix); manager.activate(one);
    auto first = manager.current(); QVERIFY(first);
    QTRY_VERIFY_WITH_TIMEOUT(first->preview.valid() || first->failed(), 20000);
    QVERIFY2(!first->failed(), qPrintable(first->status + '\n' + first->engineOutput));
    QTRY_VERIFY_WITH_TIMEOUT(first->compilerOutput.contains("OVERLAY-INCLUDED"), 10000);
    QVERIFY(!first->preview.image().isNull()); const auto original = first->preview.image();
    const auto firstPid = first->processId(); QVERIFY(firstPid > 0);
    first->preview.zoom = 1.5; first->preview.scroll = {10, 20};
    manager.activate(two); auto second = manager.current(); QVERIFY(second != first);
    QTRY_VERIFY_WITH_TIMEOUT(second->preview.valid() || second->failed(), 20000);
    QVERIFY2(!second->failed(), qPrintable(second->status + '\n' + second->engineOutput));
    QSignalSpy secondReveals(&second->preview, &TexpressoPreview::reveal), secondSources(second, &TexpressoSession::sourceLocation);
    second->syncForward(two, 2);
    QTRY_VERIFY_WITH_TIMEOUT(!secondReveals.isEmpty(), 5000);
    const auto firstPagePoint = secondReveals.last()[0].toPointF();
    second->preview.setPage(1);
    QTRY_VERIFY_WITH_TIMEOUT(second->preview.valid(), 10000);
    QSignalSpy secondCommands(&second->preview, &TexpressoPreview::command);
    second->preview.setPage(0); QVERIFY(second->preview.valid());
    QVERIFY(secondCommands.isEmpty()); // the page is displayed without a native render
    second->preview.inverse(firstPagePoint);
    QTRY_VERIFY_WITH_TIMEOUT(!secondSources.isEmpty(), 5000);
    QCOMPARE(TexpressoSession::identity(secondSources.last()[0].toString()), TexpressoSession::identity(two));
    QCOMPARE(secondSources.last()[1].toInt(), 3); // page one's source, although the engine last rendered page two
    second->preview.setPage(1); QVERIFY(second->preview.valid());
    const auto edited = prefix + " and more" + suffix;
    const int offset = prefix.size();
    manager.changeBuffer(child, 0, offset, 0, offset, " and more", edited);
    QVERIFY(!first->preview.valid());
    manager.activate(one); QCOMPARE(manager.current(), first); QCOMPARE(manager.sessionCount(), 2);
    QCOMPARE(first->processId(), firstPid);
    QCOMPARE(first->preview.zoom, 1.5); QCOMPARE(first->preview.scroll, QPoint(10, 20));
    QTRY_VERIFY_WITH_TIMEOUT(first->preview.valid() || first->failed(), 20000);
    QVERIFY2(!first->failed(), qPrintable(first->status + '\n' + first->engineOutput));
    QVERIFY(first->preview.image() != original);
    manager.changeBuffer(child, 0, offset, 0, offset + 9, QString(), prefix + suffix);
    QTRY_VERIFY_WITH_TIMEOUT(first->preview.valid(), 20000);
    QCOMPARE(first->preview.image(), original);
    QSignalSpy reveals(&first->preview, &TexpressoPreview::reveal), sources(first, &TexpressoSession::sourceLocation);
    first->syncForward(child, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!reveals.isEmpty(), 5000);
    first->preview.inverse(reveals.last()[0].toPointF());
    QTRY_VERIFY_WITH_TIMEOUT(!sources.isEmpty(), 5000);
    QCOMPARE(TexpressoSession::identity(sources.last()[0].toString()), TexpressoSession::identity(child));
    QCOMPARE(sources.last()[1].toInt(), 1);
    manager.reconcileBuffer(child, "\\undefinedcommand\n");
    QTRY_VERIFY_WITH_TIMEOUT(!first->previewError.isEmpty(), 10000);
    QVERIFY(!first->failed()); QVERIFY(!first->preview.valid());
    manager.reconcileBuffer(child, prefix + suffix);
    QTRY_VERIFY_WITH_TIMEOUT(first->preview.valid(), 10000);
    QCOMPARE(first->preview.image(), original);
    first->setPaused(true); manager.activate(two);
    QTest::qWait(150); QCOMPARE(second->preview.page(), 1);
    manager.activate(one); QVERIFY(first->userPaused);
    first->stop(); manager.activate(two); manager.activate(one); QVERIFY(first->userStopped);
    manager.restart(); QVERIFY(!manager.current()->userStopped);
    manager.shutdown(); QCOMPARE(manager.sessionCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(::kill(-firstPid, 0) == -1 && errno == ESRCH, 5000);
}
QTEST_MAIN(ClientTest)
#include "client_test.moc"
