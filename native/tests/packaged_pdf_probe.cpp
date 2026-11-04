#include <QGuiApplication>
#include <QImage>
#include <QTranslator>
#include <poppler-qt6.h>
#include <iostream>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3) return 2;
    const auto document = Poppler::Document::load(QString::fromLocal8Bit(argv[1]));
    if (!document || document->numPages() != 1) return 3;
    const auto page = document->page(0);
    if (!page || !page->text(QRectF()).contains("PDF backend example")) return 4;
    const QImage image = page->renderToImage(96, 96);
    if (image.isNull() || image.width() < 100 || image.height() < 100) return 5;
    QTranslator translator;
    if (!translator.load(QString::fromLocal8Bit(argv[2]))) return 6;
    if (translator.translate("QPlatformTheme", "Cancel").isEmpty()) return 7;
    std::cout << "Bundled Poppler: one page, text and raster verified; current Qt translation loaded\n";
    return 0;
}
