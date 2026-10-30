// Opt-in physical-pixel benchmark. Uses a prebuilt public fixture PDF so native
// raster/export/IPC/Qt decode costs cannot be confused with TeX compilation.
#include "texpressoprocess.h"
#include "texpressopreview.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QThread>
#include <algorithm>
#include <iostream>

static bool waitFor(const std::function<bool()> &ready) {
    QElapsedTimer t; t.start();
    while (!ready() && t.elapsed() < 45000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return ready();
}
static QJsonObject summary(QVector<double> samples) {
    QJsonArray raw; for (auto v : samples) raw.append(v);
    std::sort(samples.begin(), samples.end());
    return {{"samples", raw}, {"median", samples[samples.size()/2]}};
}
static QJsonObject rss(qint64 nativePid) {
    // Outside the timed request. This is sampled resident memory after frame
    // publication, not a claim about transient allocation peaks or GPU memory.
    QProcess ps; ps.start("/bin/ps", {"-o", "pid=,rss=", "-p",
        QString("%1,%2").arg(QCoreApplication::applicationPid()).arg(nativePid)});
    ps.waitForFinished(3000); QJsonObject result;
    for (const auto &row : QString::fromUtf8(ps.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
        const auto fields = row.simplified().split(' ');
        if (fields.size() == 2) result[fields[0].toLongLong() == nativePid ? "nativeBytes" : "qtBytes"] = double(fields[1].toLongLong() * 1024);
    }
    return result;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 5) { std::cerr << "engine fixture.pdf pixel-budget artifact-prefix\n"; return 1; }
    const QString engine = QString::fromLocal8Bit(argv[1]), pdf = QString::fromLocal8Bit(argv[2]);
    const int budget = QString::fromLocal8Bit(argv[3]).toInt();
    const QString artifacts = QString::fromLocal8Bit(argv[4]);
    TexpressoProcess process; TexpressoPreview preview;
    QString failure; QJsonObject capabilities, header; QByteArray png;
    QObject::connect(&process, &TexpressoProcess::ready, [&](const QJsonObject &features) {
        capabilities = features; preview.setCapabilities(features); preview.setActive(true);
    });
    QObject::connect(&process, &TexpressoProcess::message, &preview, &TexpressoPreview::message);
    QObject::connect(&preview, &TexpressoPreview::command, &process, &TexpressoProcess::send);
    QObject::connect(&process, &TexpressoProcess::image, [&](const QJsonObject &h, const QByteArray &bytes) {
        header = h; png = bytes; preview.receive(h, bytes);
    });
    QObject::connect(&process, &TexpressoProcess::failed, [&](const QString &s) { failure = s; });
    QObject::connect(&preview, &TexpressoPreview::failed, [&](const QString &s) { failure = s; });
    const auto ready = [&] { return (preview.valid() && !preview.busy()) || !failure.isEmpty(); };
    process.start(engine, pdf, {}, budget);
    if (!waitFor(ready) || !failure.isEmpty()) { std::cerr << failure.toStdString(); return 2; }
    QJsonArray cases;
    for (const int width : {1200, 1800, 2240, 3200, 4096}) {
        preview.setWidth(width); if (!waitFor(ready) || !failure.isEmpty()) return 3;
        QVector<double> elapsed[3], native[3], payload[3], qtRss[3], nativeRss[3];
        QSize sizes[3]; qint64 decodedBytes[3]{};
        // Two warmups per page, then seven independent uncached publications.
        for (int round = 0; round < 9; ++round) for (int page = 0; page < 3; ++page) {
            preview.releaseCache();
            // Alternate pages: setPage on an already valid current page is a no-op.
            if (preview.page() == page) { preview.setPage((page+1)%3); if (!waitFor(ready)) return 4; preview.releaseCache(); }
            QElapsedTimer timer; timer.start(); preview.setPage(page);
            if (!waitFor(ready) || !failure.isEmpty()) { std::cerr << failure.toStdString(); return 5; }
            const auto ms = timer.nsecsElapsed() / 1e6;
            if (header["page"].toInt() != page) return 6;
            sizes[page] = preview.image().size(); decodedBytes[page] = preview.image().sizeInBytes();
            if (round >= 2) {
                elapsed[page].append(ms); native[page].append(header["exportMs"].toDouble()); payload[page].append(png.size());
                const auto memory = rss(process.processId());
                if (memory.isEmpty()) return 7;
                qtRss[page].append(memory["qtBytes"].toDouble()); nativeRss[page].append(memory["nativeBytes"].toDouble());
            }
            if (round == 8 && page == 0 && width == 2240) {
                // Same physical destination pixels, showing the old upscale.
                const QSize target(width, qRound(width * preview.bounds().height() / preview.bounds().width()));
                QImage image(target, QImage::Format_RGB32); image.fill(Qt::white);
                QPainter painter(&image); painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(image.rect(), preview.image()); painter.end();
                image.copy(240, 340, 1200, 600).save(artifacts + "-text-crop.png");
            }
        }
        for (int page = 0; page < 3; ++page)
            cases.append(QJsonObject{{"requestedWidth", width}, {"page", page}, {"pixelWidth", sizes[page].width()},
                {"pixelHeight", sizes[page].height()}, {"decodedBytes", double(decodedBytes[page])},
                {"publicationMs", summary(elapsed[page])}, {"exportMs", summary(native[page])},
                {"payloadBytes", summary(payload[page])}, {"qtRssBytes", summary(qtRss[page])}, {"nativeRssBytes", summary(nativeRss[page])}});
    }
    process.stop(); waitFor([&] { return !process.running(); });
    const auto hash = [](const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return QString(); return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex()); };
    std::cout << QJsonDocument(QJsonObject{{"engine", engine}, {"engineSha256", hash(engine)}, {"fixtureSha256", hash(pdf)},
        {"requestedBudget", budget}, {"capabilities", capabilities}, {"qtVersion", qVersion()}, {"cases", cases},
        {"method", "Serial precompiled-PDF text/math, vectors, noisy RGB image; two warmups and seven samples/page/width. Qt cache cleared before each request; native PDF engine has no export content token. Publication includes raster/export/IPC/async Qt decode and 1ms polling, excludes widget painting. RSS sampled after publication, outside timing, excludes transient peaks/GPU. No TeX compilation in measured intervals."}}).toJson().constData();
}
