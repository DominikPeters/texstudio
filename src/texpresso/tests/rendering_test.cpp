#include <QtTest>
#include "texpressopreview.h"
#include "texpressoprocess.h"
#include "texpressowidget.h"
#include <QBuffer>
#include <QTemporaryDir>
#include <QFile>

class RenderingTest : public QObject
{
    Q_OBJECT
private slots:
    void patchesAndKeyframeRecovery();
    void latestRegionAndSourceFence();
    void unsupportedFeatures();
    void asynchronousMagnifier();
    void externalBuildDisablesInteractions();
    void realEngine();
};
static QByteArray png(const QImage &image)
{
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
static QJsonObject frame(TexpressoPreview &preview, const QJsonArray &request, const QByteArray &bytes, QSize size = {8, 8})
{
    return {{"requestId", request[1]}, {"revision", double(preview.revision())}, {"sequence", double(preview.sequence())},
        {"page", preview.page()}, {"width", size.width()}, {"height", size.height()}, {"bounds", QJsonArray{0, 0, 80, 80}},
        {"mime", "image/png"}, {"byteLength", bytes.size()}};
}
static QJsonArray lastCommand(const QSignalSpy &commands) { return commands.last()[0].toJsonArray(); }
void RenderingTest::patchesAndKeyframeRecovery()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"pngPatches", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, true, 0});
    QCOMPARE(lastCommand(commands)[0].toString(), QString("preview-png"));
    QCOMPARE(lastCommand(commands)[6].toInt(), 0);
    QImage red(8, 8, QImage::Format_RGB32); red.fill(Qt::red);
    const auto bytes = png(red); auto h = frame(preview, lastCommand(commands), bytes); h["rasterId"] = 1;
    preview.receive(h, bytes); QTRY_VERIFY(preview.valid());
    preview.sourceChanged(1); preview.message({"preview-state", 2, 1, 1, true, 0});
    QCOMPARE(lastCommand(commands)[6].toInt(), 1);
    QImage blue(2, 3, QImage::Format_RGB32); blue.fill(Qt::blue);
    const auto patch = png(blue); h = frame(preview, lastCommand(commands), patch); h["rasterId"] = 2;
    h["patch"] = QJsonObject{{"base", 1}, {"rect", QJsonArray{3, 2, 2, 3}}};
    preview.receive(h, patch); QTRY_VERIFY(preview.valid());
    QCOMPARE(preview.image().pixelColor(3, 2), QColor(Qt::blue));
    QCOMPARE(preview.image().pixelColor(2, 2), QColor(Qt::red));
    const auto composed = preview.image();
    preview.sourceChanged(2); preview.message({"preview-state", 3, 2, 1, true, 0});
    h = frame(preview, lastCommand(commands), {}); h["rasterId"] = 3;
    h["patch"] = QJsonObject{{"base", 2}, {"rect", QJsonArray{0, 0, 0, 0}}};
    preview.receive(h, {}); QTRY_VERIFY(preview.valid()); QCOMPARE(preview.image(), composed);
    QCOMPARE(preview.image().cacheKey(), composed.cacheKey()); // zero payload does no image copy
    preview.sourceChanged(3); preview.message({"preview-state", 4, 3, 1, true, 0});
    h = frame(preview, lastCommand(commands), patch); h["rasterId"] = 4;
    h["patch"] = QJsonObject{{"base", 999}, {"rect", QJsonArray{3, 2, 2, 3}}};
    preview.receive(h, patch);
    QCOMPARE(lastCommand(commands)[6].toInt(), 0); QVERIFY(!preview.valid());
    h = frame(preview, lastCommand(commands), bytes); h["rasterId"] = 5;
    preview.receive(h, bytes); QTRY_VERIFY(preview.valid()); QCOMPARE(preview.image(), red);
    // Matching raster IDs alone cannot authorize a patch for different geometry.
    preview.sourceChanged(4); preview.message({"preview-state", 5, 4, 1, true, 0});
    h = frame(preview, lastCommand(commands), patch); h["rasterId"] = 6; h["bounds"] = QJsonArray{0, 0, 81, 80};
    h["patch"] = QJsonObject{{"base", 5}, {"rect", QJsonArray{3, 2, 2, 3}}};
    preview.receive(h, patch); QCOMPARE(lastCommand(commands)[6].toInt(), 0);
}
void RenderingTest::latestRegionAndSourceFence()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    QSignalSpy regions(&preview, &TexpressoPreview::regionReady);
    preview.setCapabilities({{"regions", true}, {"pngPatches", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 2, true, 0});
    QImage red(8, 8, QImage::Format_RGB32); red.fill(Qt::red); const auto bytes = png(red);
    auto h = frame(preview, lastCommand(commands), bytes); h["rasterId"] = 1;
    preview.receive(h, bytes); QTRY_VERIFY(preview.valid()); const auto page = preview.image();
    preview.requestRegion({10, 10, 20, 20}, 100, 1); const auto first = lastCommand(commands);
    QCOMPARE(first[0].toString(), QString("preview-region"));
    for (int token = 2; token <= 100; ++token) preview.requestRegion({20, 20, 20, 20}, 100, token);
    QCOMPARE(commands.size(), 2); // one native export and only the latest queued lens position
    h = frame(preview, first, bytes); preview.receive(h, bytes);
    QTRY_COMPARE(commands.size(), 3); QCOMPARE(regions.size(), 0);
    h = frame(preview, lastCommand(commands), bytes); preview.receive(h, bytes);
    QTRY_COMPARE(regions.size(), 1); QCOMPARE(regions[0][0].toLongLong(), 100);
    QCOMPARE(preview.image().cacheKey(), page.cacheKey());
    preview.inverse({30, 30}); QCOMPARE(lastCommand(commands)[0].toString(), QString("preview-inverse"));
    preview.requestRegion({10, 10, 20, 20}, 100, 101); h = frame(preview, lastCommand(commands), bytes);
    preview.receive(h, bytes); preview.sourceChanged(1); QTest::qWait(30); QCOMPARE(regions.size(), 1);
    preview.message({"preview-state", 2, 1, 2, true, 0}); QCOMPARE(lastCommand(commands)[6].toInt(), 1);
    h = frame(preview, lastCommand(commands), bytes); h["rasterId"] = 10; preview.receive(h, bytes); QTRY_VERIFY(preview.valid());
    preview.requestRegion({10, 10, 20, 20}, 100, 102); h = frame(preview, lastCommand(commands), bytes);
    preview.cancelRegion(); preview.receive(h, bytes); QTest::qWait(30); QCOMPARE(regions.size(), 1);
}
void RenderingTest::unsupportedFeatures()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setActive(true); preview.message({"preview-state", 1, 0, 1, true, 0});
    QCOMPARE(lastCommand(commands)[0].toString(), QString("preview-render"));
    QImage red(8, 8, QImage::Format_RGB32); red.fill(Qt::red); const auto bytes = png(red);
    preview.receive(frame(preview, lastCommand(commands), bytes), bytes); QTRY_VERIFY(preview.valid());
    preview.requestRegion({10, 10, 20, 20}, 100, 1); QCOMPARE(commands.size(), 1);
}
void RenderingTest::asynchronousMagnifier()
{
    TexpressoPreview preview; QSignalSpy commands(&preview, &TexpressoPreview::command);
    preview.setCapabilities({{"regions", true}}); preview.setActive(true);
    preview.message({"preview-state", 1, 0, 1, true, 0});
    QImage red(8, 8, QImage::Format_RGB32); red.fill(Qt::red); const auto bytes = png(red);
    preview.receive(frame(preview, lastCommand(commands), bytes), bytes); QTRY_VERIFY(preview.valid());
    PDFDocumentConfig config{}; config.editTool = 1; config.magnifierSize = 120;
    config.magnifierShape = PDFDocumentConfig::Circle; config.magnifierBorder = true;
    QScrollArea area; area.resize(400, 400);
    auto canvas = new TexpressoCanvas(&config, &area); area.setWidget(canvas);
    canvas->pixels = preview.image(); canvas->pageBounds = preview.bounds(); canvas->scale = 4;
    canvas->fresh = true; canvas->resize(320, 320); canvas->setPreview(&preview);
    area.show(); QCoreApplication::processEvents();
    const QPoint point(160, 160);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point);
    auto lens = canvas->findChild<PDFMagnifier *>(); QVERIFY(lens && lens->isVisible());
    const auto color = [&] { const auto image = lens->grab().toImage(); return image.pixelColor(image.width() / 2, image.height() / 2); };
    QCOMPARE(color(), QColor(Qt::red)); // immediate crop while the native export is pending
    const auto request = lastCommand(commands); QCOMPARE(request[0].toString(), QString("preview-region"));
    QImage blue(8, 8, QImage::Format_RGB32); blue.fill(Qt::blue); const auto detail = png(blue);
    auto h = frame(preview, request, detail); h["bounds"] = QJsonArray{request[6], request[7], request[8], request[9]};
    preview.receive(h, detail); QTRY_COMPARE(color(), QColor(Qt::blue));
    config.grayscale = true; config.invertColors = true; lens->update();
    const auto converted = convertImage(QPixmap::fromImage(blue), true, true).toImage();
    QCOMPARE(color(), converted.pixelColor(0, 0));
    // A fresh native frame arriving after a source edit must not reach the lens.
    QTest::mouseMove(canvas, point + QPoint(10, 10)); const auto obsolete = lastCommand(commands);
    h = frame(preview, obsolete, bytes); h["bounds"] = QJsonArray{obsolete[6], obsolete[7], obsolete[8], obsolete[9]};
    preview.receive(h, bytes); preview.sourceChanged(1); canvas->fresh = false; canvas->resetTools();
    QTest::qWait(30); QVERIFY(!lens->isVisible());
}
void RenderingTest::externalBuildDisablesInteractions()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for external-build viewer integration");
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\\documentclass{article}\\begin{document}Build interaction test.\\end{document}\n"); file.close();
    TexpressoManager manager(nullptr, engine);
    PDFDocumentConfig config{}; config.editTool = 1; config.magnifierSize = 120;
    config.magnifierShape = PDFDocumentConfig::Circle;
    TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
    widget.resize(600, 600); widget.show(); manager.activate(root); manager.current()->setAutoRerun(false);
    auto canvas = widget.findChild<TexpressoCanvas *>(); QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->fresh, 15000);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, {100, 100});
    auto lens = canvas->findChild<PDFMagnifier *>(); QVERIFY(lens && lens->isVisible());
    manager.beginExternalBuild(root);
    QVERIFY(!canvas->fresh); QVERIFY(!lens->isVisible());
    QTRY_VERIFY(!widget.viewerActions()["gotoSource"]->isEnabled());
    QVERIFY(!widget.viewerActions()["rerunOnce"]->isEnabled());
    manager.endExternalBuild(root, false);
    QTRY_VERIFY(canvas->fresh); QVERIFY(!lens->isVisible());
}
void RenderingTest::realEngine()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE to exercise native patches and region rendering");
    QTemporaryDir dir; auto root = dir.filePath("main.tex");
    QString source = "\\documentclass{article}\n\\begin{document}\nSmall text 123456789.\\par\n";
    for (int i = 0; i < 12; ++i) source += "A whole line of unchanged text for raster patch comparison.\\par\n";
    source += "\\end{document}\n";
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(source.toUtf8()); file.close();
    root = QFileInfo(root).canonicalFilePath();
    TexpressoProcess process; TexpressoPreview preview;
    QString diagnostics;
    connect(&process, &TexpressoProcess::diagnostic, &preview, [&](const QString &line) { diagnostics += line; });
    QSignalSpy images(&process, &TexpressoProcess::image), errors(&process, &TexpressoProcess::failed);
    QSignalSpy regions(&preview, &TexpressoPreview::regionReady);
    connect(&process, &TexpressoProcess::ready, &preview, [&](const QJsonObject &capabilities) {
        preview.setCapabilities(capabilities); process.send({"rerun", false}); preview.setActive(true);
    });
    connect(&process, &TexpressoProcess::message, &preview, &TexpressoPreview::message);
    connect(&process, &TexpressoProcess::image, &preview, &TexpressoPreview::receive);
    connect(&preview, &TexpressoPreview::command, &process, &TexpressoProcess::send);
    process.start(engine, root);
    QTRY_VERIFY_WITH_TIMEOUT(preview.valid() || !errors.isEmpty(), 20000); QVERIFY(errors.isEmpty());
    const auto fullBytes = images.last()[1].toByteArray().size();
    source.replace("123456789", "123456780");
    process.send({"open", root, source}); preview.sourceChanged(1); process.send({"preview-barrier", 1});
    QTRY_VERIFY2_WITH_TIMEOUT(preview.valid(), qPrintable(diagnostics), 10000);
    auto h = images.last()[0].toJsonObject(); QVERIFY(h.contains("patch"));
    const auto patchBytes = images.last()[1].toByteArray().size(); QVERIFY(patchBytes < fullBytes);
    process.send({"open", root, source + "\n"}); preview.sourceChanged(2); process.send({"preview-barrier", 2});
    QTRY_VERIFY_WITH_TIMEOUT(preview.valid(), 10000);
    h = images.last()[0].toJsonObject(); QVERIFY(h.contains("patch"));
    QCOMPARE(images.last()[1].toByteArray().size(), 0);
    const auto page = preview.image();
    // Locate ink and request the same physical area at substantially higher
    // resolution. The decoded detail contains genuinely new samples.
    QPoint ink;
    for (int y = 0; y < page.height() && ink.isNull(); ++y)
        for (int x = 0; x < page.width(); ++x) if (qGray(page.pixel(x, y)) < 100) { ink = {x, y}; break; }
    QVERIFY(!ink.isNull());
    const QRect sample = QRect(ink - QPoint(8, 8), QSize(100, 50)).intersected(page.rect());
    const auto bounds = preview.bounds();
    const QRectF region(bounds.left() + sample.x() * bounds.width() / page.width(),
        bounds.top() + sample.y() * bounds.height() / page.height(), sample.width() * bounds.width() / page.width(),
        sample.height() * bounds.height() / page.height());
    preview.requestRegion(region, 600, 42);
    QTRY_COMPARE_WITH_TIMEOUT(regions.size(), 1, 10000);
    const auto detail = qvariant_cast<QImage>(regions[0][1]); QVERIFY(detail.width() >= 590);
    const auto enlarged = page.copy(sample).scaled(detail.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QVERIFY(detail != enlarged); QCOMPARE(preview.image().cacheKey(), page.cacheKey());
    qInfo() << "Native full PNG bytes:" << fullBytes << "small edit patch bytes:" << patchBytes
        << "no-op bytes: 0; region pixels:" << detail.size();
    process.stop();
}
QTEST_MAIN(RenderingTest)
#include "rendering_test.moc"
