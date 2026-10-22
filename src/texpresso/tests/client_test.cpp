#include <QtTest>
#include "texpressoprotocol.h"
#include "texpressomanager.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QBuffer>
#include <csignal>

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
static void write(const QString &path, const QString &text) {
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(text.toUtf8());
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
    second->preview.setPage(1);
    QTRY_VERIFY_WITH_TIMEOUT(second->preview.valid(), 10000);
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
