// Opt-in Qt end-to-end page-return timings, with the Qt image cache bypassed.
// Compare engine snapshots using the same executable; timings are not tests.
#include "texpressomanager.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>

static bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return ready();
}

static QString pixelHash(const QImage &pixels)
{
    const auto image = pixels.convertToFormat(QImage::Format_RGBA8888);
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(image.constBits()), int(image.sizeInBytes()));
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    QFile binary(engine);
    if (!binary.open(QIODevice::ReadOnly)) return 1;
    const auto sha = QCryptographicHash::hash(binary.readAll(), QCryptographicHash::Sha256).toHex();
    QTemporaryDir project;
    const auto root = project.filePath("main.tex");
    QString tex = "\\documentclass{article}\n\\begin{document}\n";
    for (int page = 0; page < 3; ++page) {
        if (page) tex += "\\newpage\n";
        tex += QString("\\section{Page %1}\n").arg(page + 1);
        for (int i = 0; i < 16; ++i)
            tex += "Native PNG page rendering, live compilation, and source navigation through an ordinary text document.\\par\n";
    }
    tex += "\\end{document}\n";
    QFile file(root); if (!file.open(QIODevice::WriteOnly)) return 2;
    file.write(tex.toUtf8()); file.close();
    TexpressoManager manager(nullptr, engine); manager.activate(root);
    auto session = manager.current(); session->followCursor = false;
    session->setAutoRerun(false); session->preview.setWidth(1600);
    int requests = 0;
    QObject::connect(&session->preview, &TexpressoPreview::command, [&](const QJsonArray &c) {
        if (c[0] == "preview-png" || c[0] == "preview-render") ++requests;
    });
    const auto ready = [&] { return session->preview.valid() && !session->preview.busy(); };
    if (!waitFor([&] { return ready() || session->failed(); }) || session->failed()) {
        std::cerr << session->engineOutput.toStdString(); return 3;
    }
    // A live session normally compiles only as far as preview demand. Demand
    // EOF first so compilation cannot overlap the measured page-return loop.
    session->preview.setPage(100000);
    if (!waitFor([&] { return ready() && session->preview.complete(); })) return 9;
    session->preview.setPage(0);
    if (!waitFor(ready)) return 10;
    const QImage page0 = session->preview.image();
    session->preview.setPage(1);
    if (!waitFor(ready)) return 4;
    const QImage page1 = session->preview.image();
    if (page0 == page1) return 5;
    QVector<double> samples;
    const int before = requests;
    for (int i = 0; i < 16; ++i) {
        // Force native export, IPC and asynchronous Qt PNG decode each time.
        session->preview.releaseCache();
        QElapsedTimer timer; timer.start(); session->preview.setPage(i % 2);
        if (!waitFor(ready)) return 6;
        samples.append(timer.nsecsElapsed() / 1e6);
        if (session->preview.image() != (i % 2 ? page1 : page0)) return 7;
    }
    if (requests - before != samples.size()) return 8;
    QJsonArray raw; for (double value : samples) raw.append(value);
    std::sort(samples.begin(), samples.end());
    const QJsonObject report{{"engine", engine}, {"engineSha256", QString::fromLatin1(sha)},
        {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(tex.toUtf8(), QCryptographicHash::Sha256).toHex())},
        {"pagePixelSha256", QJsonArray{pixelHash(page0), pixelHash(page1)}},
        {"width", 1600}, {"samplesMs", raw}, {"medianMs", (samples[7] + samples[8]) / 2},
        {"nativeRequests", requests - before}, {"identicalPixels", true},
        {"method", "Alternating pages 0/1 after priming; Qt LRU cleared before each return; native export, IPC and Qt decode/publication; 1ms event polling; excludes widget painting"}};
    std::cout << QJsonDocument(report).toJson().constData();
    manager.shutdown();
}
