#ifndef TEXPRESSOPREVIEW_H
#define TEXPRESSOPREVIEW_H
#include <QObject>
#include <QImage>
#include <QRectF>
#include <QJsonArray>
#include <QJsonObject>
#include <optional>
#include <QList>

class TexpressoPreview : public QObject
{
    Q_OBJECT
public:
    explicit TexpressoPreview(QObject *parent = nullptr) : QObject(parent) {}
    void sourceChanged(qint64 sequence);
    void message(const QJsonArray &message);
    void receive(const QJsonObject &header, const QByteArray &bytes);
    void setCapabilities(const QJsonObject &capabilities);
    void requestRegion(const QRectF &bounds, int width, qint64 token);
    void cancelRegion();
    void setActive(bool active);
    void setPage(int page);
    void setWidth(int width);
    void inverse(const QPointF &point);
    void forward(const QString &path, int zeroBasedLine);
    void releaseCache();
    const QImage &image() const { return pixels; }
    QRectF bounds() const { return pageBounds; }
    int page() const { return currentPage; }
    int pageCount() const { return count; }
    bool complete() const { return finished; }
    bool valid() const { return fresh; }
    qint64 sequence() const { return sourceSequence; }
    qint64 revision() const { return engineRevision; }
    double zoom = 0; // 0 = fit width; retained per project.
    QPoint scroll;
signals:
    void command(const QJsonArray &command);
    void updated();
    void failed(const QString &reason);
    void reveal(const QPointF &point);
    void regionReady(qint64 token, const QImage &image, const QRectF &bounds);
private:
    struct Frame { QJsonObject header; QByteArray bytes; QImage base; bool region; qint64 token; };
    struct Region { QRectF bounds; int width; qint64 token; };
    void request();
    void decode(Frame frame);
    bool matches(const QJsonObject &header) const;
    struct CachedPage { int page, width; QImage image; QRectF bounds; };
    void clearCache();
    void cachePage();
    bool restorePage();
    void finishInverse();
    void retryFullFrame();
    QImage pixels;
    QRectF pageBounds;
    qint64 sourceSequence = 0, engineRevision = -1, nextRequest = 0, inFlight = 0;
    int currentPage = 0, count = 0, pixelWidth = 1200;
    bool active = false, finished = false, dirty = true, fresh = false, decoding = false;
    bool waitingForward = false;
    std::optional<Frame> pending;
    QList<CachedPage> pages; // LRU, at most four images / 32 MiB per session.
    qint64 cacheBytes = 0;
    bool needsNativePage = false;
    std::optional<QPointF> pendingInverse;
    bool supportsRegions = false, supportsPatches = false, renderingRegion = false;
    std::optional<Region> pendingRegion;
    qint64 regionToken = -1, flightRegionToken = -1, rasterId = 0;
    int rasterPage = -1;
    QRectF rasterBounds;
    QSize rasterSize;
};
#endif
