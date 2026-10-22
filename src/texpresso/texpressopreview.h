#ifndef TEXPRESSOPREVIEW_H
#define TEXPRESSOPREVIEW_H
#include <QObject>
#include <QImage>
#include <QRectF>
#include <QJsonArray>
#include <QJsonObject>
#include <optional>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QUrl>

class TexpressoPreview : public QObject
{
    Q_OBJECT
public:
    explicit TexpressoPreview(QObject *parent = nullptr);
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
    void setViewportPages(const QList<int> &visible, bool gesture = false);
    QImage pageImage(int page) const;
    QRectF boundsForPage(int page) const;
    bool pageValid(int page) const;
    QJsonObject annotationsForPage(int page) const;
    void activateLink(int page, int index, qint64 revision, qint64 sequence);
    bool busy() const;
    void setPreparationPending(bool pending);
    qint64 nextRequestId() { return ++nextRequest; }
    void invalidateNativePage() { needsNativePage = true; }
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
    void interactionStarted();
    void activityChanged();
    void updated();
    void failed(const QString &reason);
    void reveal(const QPointF &point);
    void regionReady(qint64 token, const QImage &image, const QRectF &bounds);
    void externalLinkRequested(const QUrl &url);
private:
    friend class RerunTest;
    friend class LinksTest;
    struct Frame { QJsonObject header; QByteArray bytes; QImage base; bool region; qint64 token; bool background = false; };
    struct Region { QRectF bounds; int width; qint64 token; };
    void request();
    void decode(Frame frame);
    bool matches(const QJsonObject &header) const;
    struct CachedPage { int page, width; QImage image; QRectF bounds; QJsonObject annotations; };
    void clearCache();
    void cachePage();
    void storePage(int page, int width, const QImage &image, const QRectF &bounds, const QJsonObject &annotations = {});
    void cancelDestination(bool notifyEngine = true);
    void finishDestination();
    void interruptBackground();
    void schedulePrefetch();
    bool restorePage();
    void finishInverse();
    void retryFullFrame();
    QImage pixels;
    QRectF pageBounds;
    QJsonObject pageAnnotations;
    struct Destination { QString name; qint64 revision, sequence; int page = -1; };
    std::optional<Destination> destination;
    bool settingDestinationPage = false;
    qint64 sourceSequence = 0, engineRevision = -1, nextRequest = 0, inFlight = 0;
    int currentPage = 0, count = 0, pixelWidth = 1200;
    bool active = false, finished = false, dirty = true, fresh = false, decoding = false;
    bool waitingForward = false;
    std::optional<Frame> pending;
    QList<CachedPage> pages; // LRU, four entries normally, up to sixteen for zoomed-out visible pages / 32 MiB.
    qint64 cacheBytes = 0;
    QList<int> visiblePages;
    QSet<int> prefetched;
    QTimer prefetchTimer, gestureTimer, forwardTimer, destinationTimer;
    bool supportsLinks = false, supportsBackground = false, renderingBackground = false, gestureActive = false, preparationPending = false;
    bool prefetchReady = false, pumping = false;
    int flightPage = 0, flightWidth = 1200, frontierBudget = 1;
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
