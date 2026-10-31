// In-memory codec comparison using the production PNG writer, libwebp, and
// the actual Qt decoders. Rasterization and file I/O stay outside codec timing.
#include <QCoreApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <webp/encode.h>
#include <sys/resource.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstring>
#include <iostream>
extern "C" {
#include "preview.h"
}

static QJsonObject summary(QVector<double> samples) {
    QJsonArray raw; for (auto v : samples) raw.append(v);
    std::sort(samples.begin(), samples.end());
    return {{"samples", raw}, {"median", samples[samples.size()/2]}};
}
static double peakRss() {
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return double(usage.ru_maxrss);
#else
    return double(usage.ru_maxrss) * 1024;
#endif
}
static bool gray(const QImage &image) {
    for (int y=0;y<image.height();++y) {
        const auto row=image.constScanLine(y);
        for (int x=0;x<image.width();++x) if (row[3*x]!=row[3*x+1] || row[3*x]!=row[3*x+2]) return false;
    }
    return true;
}
static QJsonObject difference(const QImage &reference, const QImage &input) {
    const auto image=input.convertToFormat(QImage::Format_RGB888);
    double squared=0, inkSquared=0; qint64 changed=0, whiteChanged=0, ink=0, lostDark=0; int worst=0;
    for (int y=0;y<image.height();++y) {
        const auto a=reference.constScanLine(y),b=image.constScanLine(y);
        for (int x=0;x<image.width();++x) {
            bool different=false; double s=0; int minA=255,maxA=0,minB=255;
            for (int c=0;c<3;++c) {
                const int av=a[3*x+c],bv=b[3*x+c],d=std::abs(av-bv);
                different |= d>0; worst=std::max(worst,d); s+=d*d;
                minA=std::min(minA,av);maxA=std::max(maxA,av);minB=std::min(minB,bv);
            }
            squared+=s; changed+=different; whiteChanged+=different && minA==255;
            if (minA<250) { ink++; inkSquared+=s; }
            lostDark+=maxA<64 && minB>128;
        }
    }
    const double pixels=double(image.width())*image.height(),mse=squared/(pixels*3);
    return {{"identical",changed==0},{"changedPixelFraction",changed/pixels},
        {"maxChannelError",worst},{"mse",mse},{"psnrDb",mse?QJsonValue(10*std::log10(255.0*255/mse)):QJsonValue()},
        {"inkPixels",double(ink)},{"inkMse",ink?inkSquared/(ink*3):0},
        {"originalWhitePixelsChanged",double(whiteChanged)},{"darkPixelsLightenedPast128",double(lostDark)}};
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (argc!=7) { std::cerr<<"fixture.pdf page width page|word|paragraph codec output-prefix\n"; return 1; }
    const QString pdf=QString::fromLocal8Bit(argv[1]),kind=QString::fromLocal8Bit(argv[4]);
    const QString mode=QString::fromLocal8Bit(argv[5]),prefix=QString::fromLocal8Bit(argv[6]);
    const int pageNumber=QString::fromLocal8Bit(argv[2]).toInt(),width=QString::fromLocal8Bit(argv[3]).toInt();
    if (width<512 || width>4096 || pageNumber<0 || (kind!="page" && kind!="word" && kind!="paragraph")) return 1;
    const bool png=mode=="png";
    WebPConfig config; WebPConfigInit(&config);
    if (!png) {
        if (mode=="ll0" || mode=="ll1" || mode=="ll0-mt" || mode=="near80" || mode=="near60" || mode=="near80-l1" || mode=="near60-l1") {
            WebPConfigLosslessPreset(&config,(mode=="ll1" || mode.endsWith("-l1"))?1:0);
            config.near_lossless=mode.startsWith("near80")?80:mode.startsWith("near60")?60:100;
            config.thread_level=mode=="ll0-mt";
        } else if (mode.startsWith("q")) {
            const int quality=mode.section('-',0,0).mid(1).toInt();
            if (quality<1 || quality>100) return 2;
            WebPConfigPreset(&config,mode.endsWith("-default")?WEBP_PRESET_DEFAULT:WEBP_PRESET_TEXT,quality);config.method=0;
            config.use_sharp_yuv=mode.endsWith("-sharp");
            config.thread_level=mode.endsWith("-mt");
        } else return 3;
        config.exact=1;
        if (!WebPValidateConfig(&config)) return 4;
    }
    const auto supported=QImageReader::supportedImageFormats();
    if (!supported.contains(png?"png":"webp")) { std::cerr<<"Qt codec plugin unavailable\n"; return 5; }
    auto ctx=fz_new_context(nullptr,nullptr,FZ_STORE_DEFAULT); fz_register_document_handlers(ctx);
    auto document=fz_open_document(ctx,pdf.toLocal8Bit().constData());
    auto page=fz_load_page(ctx,document,pageNumber);auto list=fz_new_display_list_from_page(ctx,page);
    const auto bounds=fz_bound_display_list(ctx,list);
    const auto size=txp_preview_raster_size(bounds.x1-bounds.x0,bounds.y1-bounds.y0,width,TXP_PREVIEW_MAX_PIXELS);
    const float scale=float(size.x1)/(bounds.x1-bounds.x0);
    auto full=fz_new_pixmap_with_bbox(ctx,fz_device_rgb(ctx),size,nullptr,0);fz_clear_pixmap_with_value(ctx,full,255);
    auto device=fz_new_draw_device(ctx,fz_identity,full);
    fz_run_display_list(ctx,list,device,fz_concat(fz_translate(-bounds.x0,-bounds.y0),fz_scale(scale,scale)),fz_rect_from_irect(size),nullptr);
    fz_close_device(ctx,device);fz_drop_device(ctx,device);fz_drop_display_list(ctx,list);
    fz_drop_page(ctx,page);fz_drop_document(ctx,document);
    fz_pixmap *pixels=full;
    if (kind!="page") {
        const qreal f=width/2240.0;
        const QRect crop(qRound(445*f),qRound(825*f),qRound((kind=="word"?256:1200)*f),qRound((kind=="word"?80:160)*f));
        pixels=fz_new_pixmap(ctx,fz_device_rgb(ctx),crop.width(),crop.height(),nullptr,0);
        for (int y=0;y<crop.height();++y) std::memcpy(pixels->samples+y*pixels->stride,
            full->samples+(y+crop.y())*full->stride+crop.x()*3,crop.width()*3);
    }
    const QImage reference(pixels->samples,pixels->w,pixels->h,pixels->stride,QImage::Format_RGB888);
    QVector<double> encode,decode,cpu,storage,grayConversion,grayDetection;QImage last;QByteArray encoded;
    const bool grayscale=gray(reference);
    for (int i=0;i<9;++i) {
        QElapsedTimer timer;timer.start();const auto cpuStart=std::clock();
        fz_buffer *buffer=nullptr;WebPMemoryWriter writer;WebPMemoryWriterInit(&writer);
        if (png) buffer=txp_preview_png(ctx,pixels);
        else {
            WebPPicture picture;WebPPictureInit(&picture);picture.use_argb=config.lossless || config.use_sharp_yuv;
            picture.width=pixels->w;picture.height=pixels->h;
            picture.writer=WebPMemoryWrite;picture.custom_ptr=&writer;
            if (!WebPPictureImportRGB(&picture,pixels->samples,pixels->stride) || !WebPEncode(&config,&picture)) return 6;
            WebPPictureFree(&picture);
        }
        const double encodeMs=timer.nsecsElapsed()/1e6,cpuMs=1000.0*(std::clock()-cpuStart)/CLOCKS_PER_SEC;
        const QByteArray bytes=QByteArray::fromRawData(png?reinterpret_cast<char*>(buffer->data):reinterpret_cast<char*>(writer.mem),png?buffer->len:writer.size);
        timer.restart();QImage image;
        {
            QBuffer input;input.setData(bytes);input.open(QIODevice::ReadOnly);
            QImageReader reader(&input,png?"png":"webp");image=reader.read();
            if (image.size()!=reference.size()) { std::cerr<<reader.errorString().toStdString(); return 7; }
        }
        const auto decodeMs=timer.nsecsElapsed()/1e6;
        if (i>=2) { encode.append(encodeMs);cpu.append(cpuMs);decode.append(decodeMs);storage.append(image.sizeInBytes()); }
        if (i==8) { last=image;encoded=QByteArray(bytes.constData(),bytes.size()); }
        fz_drop_buffer(ctx,buffer);WebPMemoryWriterClear(&writer);
    }
    const double rss=peakRss();
    // Optional memory recovery for neutral pages. The detection cost is separate:
    // a negotiated grayscale flag would need this work in the encoder as well.
    for (int i=0;i<7;++i) {
        QElapsedTimer timer;timer.start();const bool isGray=gray(reference);
        grayDetection.append(timer.nsecsElapsed()/1e6);if(isGray!=grayscale) return 8;
        if (grayscale) {timer.restart();const auto reduced=last.convertToFormat(QImage::Format_Grayscale8);grayConversion.append(timer.nsecsElapsed()/1e6);if(reduced.isNull()) return 9;}
    }
    const auto quality=difference(reference,last);
    if ((png || mode=="ll0" || mode=="ll0-mt" || mode=="ll1") && !quality["identical"].toBool()) return 10;
    QFile output(prefix+(png?".png":".webp"));if(!output.open(QIODevice::WriteOnly) || output.write(encoded)!=encoded.size()) return 11;
    const qreal f=width/2240.0;
    const QRect crop=kind=="page"?QRect(qRound((pageNumber==1?480:240)*f),qRound((pageNumber==1?700:340)*f),qRound((pageNumber==1?600:1200)*f),qRound(600*f)):last.rect();
    if(!last.copy(crop).save(prefix+"-crop.png") || !reference.copy(crop).save(prefix+"-reference.png")) return 12;
    QJsonObject report{{"codec",mode},{"page",pageNumber},{"kind",kind},{"requestedWidth",width},{"width",pixels->w},{"height",pixels->h},
        {"qtVersion",qVersion()},{"qtImageFormat",int(last.format())},{"webpVersion",WebPGetEncoderVersion()},{"grayscaleSource",grayscale},
        {"encodeMs",summary(encode)},{"encodeCpuMs",summary(cpu)},{"decodeMs",summary(decode)},
        {"decodedBytes",summary(storage)},{"encodedBytes",encoded.size()},{"peakRssBytesBeforeQualityAnalysis",rss},
        {"grayscaleDetectionMs",summary(grayDetection)},{"quality",quality},
        {"webpSettings",QJsonObject{{"lossless",config.lossless},{"quality",config.quality},{"method",config.method},
            {"nearLossless",config.near_lossless},{"threads",config.thread_level},{"sharpYuv",config.use_sharp_yuv},
            {"preset",png?"not applicable":config.lossless?"LosslessPreset":mode.endsWith("-default")?"DEFAULT":"TEXT"}}}};
    if(grayscale) report["optionalGrayscaleConversionMs"]=summary(grayConversion);
    std::cout<<QJsonDocument(report).toJson().constData();
    if(pixels!=full)fz_drop_pixmap(ctx,pixels);fz_drop_pixmap(ctx,full);fz_drop_context(ctx);
}
