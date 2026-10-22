// Local, opt-in client timings; no timing thresholds are used as tests.
#include "texpressowidget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QThread>
#include <QPainter>
#include <algorithm>
#include <iostream>

static bool waitFor(const std::function<bool()> &ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 20000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return ready();
}
static QJsonObject summary(QVector<double> values) {
    std::sort(values.begin(), values.end()); QJsonArray samples;
    for (auto value : values) samples.append(value);
    return {{"medianMs", values[values.size() / 2]}, {"samplesMs", samples}};
}
class PaintCounter : public QObject {
public: int count = 0;
    bool eventFilter(QObject *, QEvent *event) override { if (event->type() == QEvent::Paint) ++count; return false; }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    PDFDocumentConfig config{}; config.editTool = 1; config.zoomStepFactor = 1.4142135;
    config.magnifierSize = 300; config.magnifierShape = PDFDocumentConfig::Circle;
    config.magnifierShadow = true;
    QJsonObject report;
    QScrollArea area; area.resize(800, 1000);
    auto canvas = new TexpressoCanvas(&config, &area);
    canvas->pixels = QImage(1600, 2200, QImage::Format_RGB32);
    for (int y = 0; y < canvas->pixels.height(); ++y) {
        auto line = reinterpret_cast<QRgb *>(canvas->pixels.scanLine(y));
        for (int x = 0; x < canvas->pixels.width(); ++x) line[x] = qRgb((x * 13 + y) % 256, (x + y * 7) % 256, (x + y) % 256);
    }
    canvas->pageBounds = {0, 0, 600, 825}; canvas->resize(800, 1100); canvas->scale = 800.0 / 600; canvas->fresh = true;
    area.setWidget(canvas); area.show(); QCoreApplication::processEvents();
    QImage target(canvas->size(), QImage::Format_RGB32);
    for (bool grayscale : {false, true}) {
        config.grayscale = grayscale;
        QVector<double> times;
        for (int i = 0; i < 25; ++i) {
            QElapsedTimer timer; timer.start(); canvas->render(&target);
            if (i >= 5) times.append(timer.nsecsElapsed() / 1e6);
        }
        report[grayscale ? "repaintGrayscale" : "repaintColor"] = summary(times);
    }
    config.grayscale = false;
    const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
    if (!engine.isEmpty()) {
        QTemporaryDir project; const auto root = project.filePath("main.tex");
        QString tex = "\\documentclass{article}\n\\begin{document}\n";
        for (int page = 0; page < 3; ++page) {
            if (page) tex += "\\newpage\n";
            tex += QString("\\section{Page %1}\n").arg(page + 1);
            for (int i = 0; i < 8; ++i) tex += "Live compilation and native Qt rendering. Some mathematics $E=mc^2$ and source navigation.\\par\n";
        }
        tex += "\\end{document}\n";
        QFile file(root); if (!file.open(QIODevice::WriteOnly)) return 1; file.write(tex.toUtf8()); file.close();
        TexpressoManager manager(nullptr, engine);
        TexpressoWidget widget(&manager, &config, [](const QString &) { return QIcon(); });
        widget.resize(800, 1000); widget.show(); manager.activate(root);
        auto session = manager.current();
        int renders = 0;
        QObject::connect(&session->preview, &TexpressoPreview::command, [&](const QJsonArray &c) { if (c[0] == "preview-render") ++renders; });
        if (!waitFor([&] { return session->preview.valid() || session->failed(); }) || session->failed()) return 2;
        // Settle initial viewport sizing, then prime both pages at that width.
        QElapsedTimer settle; settle.start(); while (settle.elapsed() < 250) { QCoreApplication::processEvents(); QThread::msleep(1); }
        session->preview.setPage(1); if (!waitFor([&] { return session->preview.valid(); })) return 3;
        const int before = renders;
        QVector<double> navigation;
        for (int i = 0; i < 12; ++i) {
            QElapsedTimer timer; timer.start(); session->preview.setPage(i % 2);
            if (!waitFor([&] { return session->preview.valid(); })) return 4;
            navigation.append(timer.nsecsElapsed() / 1e6);
        }
        report["pageRevisit"] = summary(navigation); report["revisitNativeRenders"] = renders - before;
        QCoreApplication::processEvents(); QCoreApplication::processEvents();
        auto viewCanvas = widget.findChild<TexpressoCanvas *>(); PaintCounter paints; viewCanvas->installEventFilter(&paints);
        QElapsedTimer timer; timer.start();
        for (int i = 0; i < 1000; ++i) { manager.updated(); if (i % 100 == 99) QCoreApplication::processEvents(); }
        QCoreApplication::processEvents();
        report["statusBurst1000Ms"] = timer.nsecsElapsed() / 1e6; report["statusBurstPaints"] = paints.count;
        manager.shutdown();
    }
    std::cout << QJsonDocument(report).toJson().constData();
}
