#include <QtTest>
#include "texpressopreview.h"
#include "texpressoprocess.h"
#include "texpressowidget.h"
#include <QBuffer>
#include <QTemporaryDir>
#include <QFile>
#include <QScopeGuard>

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
    void cachedInverseUsesPublishedSourceMap();
    void retinaResolution();
};
void RenderingTest::retinaResolution()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE for native Retina rendering");
    QTemporaryDir project; const auto root = project.filePath("retina.tex");
    QString source = "\\documentclass[a4paper]{article}\n\\begin{document}\nFirst page A.\\newpage\nSecond page B.\\end{document}\n";
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(source.toUtf8()); file.close();
    TexpressoManager manager(nullptr, engine); manager.openBuffer(root, source); manager.activate(root);
    auto session = manager.current(); session->followCursor = false; session->setAutoRerun(false);
    auto &preview = session->preview;
    QTRY_VERIFY_WITH_TIMEOUT(preview.valid() || session->failed(), 45000);
    QVERIFY2(!session->failed(), qPrintable(session->engineOutput));
    preview.lastPage(); QTRY_VERIFY_WITH_TIMEOUT(preview.complete() && preview.valid(), 15000);
    for (int width : {2240, 3200}) {
        preview.setWidth(width); preview.navigateTo(0);
        QTRY_VERIFY_WITH_TIMEOUT(preview.valid() && preview.image().width() == width, 15000);
        const auto first = preview.image();
        QVERIFY(qint64(first.width()) * first.height() > 4000000);
        preview.navigateTo(1); QTRY_VERIFY_WITH_TIMEOUT(preview.valid() && preview.image().width() == width, 15000);
        QVERIFY(preview.image() != first);
        preview.releaseCache(); preview.navigateTo(0);
        QTRY_VERIFY_WITH_TIMEOUT(preview.valid() && preview.image().width() == width, 15000);
        QCOMPARE(preview.image(), first); // Includes native encoded-only cache reuse at 3200 px.
    }
    const auto sequence = preview.sequence();
    const auto beforeEdit = preview.image();
    source.replace("page A", "page C"); manager.openBuffer(root, source);
    QTRY_VERIFY_WITH_TIMEOUT(preview.sequence() > sequence && preview.valid(), 15000);
    QCOMPARE(preview.image().width(), 3200); // A changed page still decodes/composes at Retina size.
    QVERIFY(preview.image() != beforeEdit);
    manager.shutdown();
}
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
void RenderingTest::cachedInverseUsesPublishedSourceMap()
{
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (engine.isEmpty()) QSKIP("Set TEXPRESSO_TEST_ENGINE");
    const auto oldProfile = qgetenv("TEXPRESSO_PROFILE"); qputenv("TEXPRESSO_PROFILE", "1");
    const auto restore = qScopeGuard([&] { if (oldProfile.isNull()) qunsetenv("TEXPRESSO_PROFILE"); else qputenv("TEXPRESSO_PROFILE", oldProfile); });
    QTemporaryDir dir; const auto root = dir.filePath("main.tex");
    QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\\documentclass{article}\n\\pagestyle{empty}\n\\begin{document}\nFirst source line.\n\\newpage\nSecond source line.\n\\newpage\nThird source line.\n\\end{document}\n"); file.close();
    TexpressoProcess process; TexpressoPreview preview; QString diagnostics;
    QSignalSpy images(&process, &TexpressoProcess::image), messages(&process, &TexpressoProcess::message), commands(&preview, &TexpressoPreview::command);
    connect(&process, &TexpressoProcess::diagnostic, &preview, [&](const QString &text) { diagnostics += text; });
    connect(&process, &TexpressoProcess::ready, &preview, [&](QJsonObject capabilities) {
        capabilities["backgroundRendering"] = false; preview.setCapabilities(capabilities);
        process.send({"rerun", false}); preview.setActive(true);
    });
    connect(&process, &TexpressoProcess::message, &preview, &TexpressoPreview::message);
    connect(&process, &TexpressoProcess::image, &preview, &TexpressoPreview::receive);
    connect(&preview, &TexpressoPreview::command, &process, &TexpressoProcess::send);
    process.start(engine, root); QTRY_VERIFY2_WITH_TIMEOUT(preview.valid(), qPrintable(diagnostics), 20000);
    const auto text = images.last()[0].toJsonObject()["textBounds"].toArray(); QCOMPARE(text.size(), 4);
    const QPointF point((text[0].toDouble() + text[2].toDouble()) / 2, (text[1].toDouble() + text[3].toDouble()) / 2);
    preview.setPage(1); QTRY_VERIFY_WITH_TIMEOUT(preview.valid() && preview.page() == 1, 10000);
    preview.setPage(0); QVERIFY(preview.valid());
    commands.clear(); messages.clear(); images.clear(); diagnostics.clear();
    QElapsedTimer timer; timer.start(); preview.inverse(point);
    const auto hasSource = [&] { for (const auto &row : messages) if (row[0].toJsonArray()[0] == "synctex") return true; return false; };
    QTRY_VERIFY2_WITH_TIMEOUT(hasSource(), qPrintable(diagnostics), 3000);
    qInfo() << "Cached inverse search returned in" << timer.elapsed() << "ms";
    QCOMPARE(commands.size(), 1); QCOMPARE(lastCommand(commands)[0].toString(), QString("preview-inverse"));
    QVERIFY(images.isEmpty());
    QVERIFY2(!diagnostics.contains("dvi-display-list"), qPrintable(diagnostics));
    QVERIFY2(!diagnostics.contains("compiler-restore"), qPrintable(diagnostics));
    for (const auto &row : messages) if (row[0].toJsonArray()[0] == "synctex")
        QCOMPARE(row[0].toJsonArray()[2].toInt(), 4);
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
    const auto textBounds = images.last()[0].toJsonObject()["textBounds"].toArray();
    QCOMPARE(textBounds.size(), 4);
    QVERIFY(!preview.horizontalTextRange().isEmpty());
    const auto fullBytes = images.last()[1].toByteArray().size();
    source.replace("123456789", "123456780");
    process.send({"open", root, source}); preview.sourceChanged(1); process.send({"preview-barrier", 1});
    QTRY_VERIFY2_WITH_TIMEOUT(preview.valid(), qPrintable(diagnostics), 10000);
    auto h = images.last()[0].toJsonObject(); QVERIFY(h.contains("patch"));
    QCOMPARE(h["textBounds"].toArray().size(), 4);
    const auto patchBytes = images.last()[1].toByteArray().size(); QVERIFY(patchBytes < fullBytes);
    process.send({"open", root, source + "\n"}); preview.sourceChanged(2); process.send({"preview-barrier", 2});
    QTRY_VERIFY_WITH_TIMEOUT(preview.valid(), 10000);
    h = images.last()[0].toJsonObject(); QVERIFY(h.contains("patch"));
    QCOMPARE(images.last()[1].toByteArray().size(), 0);
    QCOMPARE(h["textBounds"].toArray().size(), 4); // Metadata survives zero-byte updates.
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
