#include "texpressopreview.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QBuffer>
#include <QImageReader>
#include <algorithm>

void TexpressoPreview::sourceChanged(qint64 sequence)
{
    sourceSequence = sequence; engineRevision = -1;
    dirty = true; fresh = false; inFlight = 0; pending.reset();
    waitingForward = false;
    clearCache();
    emit updated();
}
void TexpressoPreview::setActive(bool enabled)
{
    active = enabled;
    if (active) request();
}
void TexpressoPreview::setPage(int page)
{
    waitingForward = false;
    pendingInverse.reset();
    const int maximum = finished ? std::max(0, count - 1) : 100000;
    page = std::clamp(page, 0, maximum);
    if (page == currentPage) return;
    currentPage = page; dirty = true; fresh = false; inFlight = 0; pending.reset();
    scroll = {}; emit updated(); request();
}
void TexpressoPreview::setWidth(int width)
{
    width = std::clamp(width, 1, 4096);
    if (width == pixelWidth) return;
    pixelWidth = width; dirty = true; inFlight = 0;
    request();
}
void TexpressoPreview::message(const QJsonArray &m)
{
    const auto verb = m[0].toString();
    if (verb == "preview-state" && m.size() == 6 && m[2].toDouble() == sourceSequence) {
        const auto revision = qint64(m[1].toDouble());
        if (revision != engineRevision) { inFlight = 0; dirty = true; fresh = false; clearCache(); }
        engineRevision = revision;
        if (m[5].toInt() != currentPage) needsNativePage = true;
        count = std::max(0, m[3].toInt()); finished = m[4].toBool();
        if (finished && count && currentPage >= count) setPage(count - 1);
        emit updated(); request();
    } else if (verb == "preview-invalidated" && m.size() == 3 && m[2].toDouble() == sourceSequence) {
        clearCache();
        engineRevision = qint64(m[1].toDouble()); dirty = true; fresh = false; inFlight = 0;
        emit updated(); request();
    } else if (verb == "preview-retry" && m.size() == 2 && m[1].toDouble() == inFlight) {
        inFlight = 0; dirty = true; request();
    } else if (verb == "preview-error" && m.size() == 3 && m[1].toDouble() == inFlight) {
        inFlight = 0; pendingInverse.reset(); emit failed(m[2].toString());
    } else if (verb == "preview-forward" && waitingForward && m.size() == 6 && m[1].toDouble() == engineRevision && m[2].toDouble() == sourceSequence) {
        setPage(m[3].toInt()); emit reveal(QPointF(m[4].toDouble(), m[5].toDouble()));
    }
}
void TexpressoPreview::request()
{
    if ((!active && !pendingInverse) || !dirty || inFlight || engineRevision < 0 || (finished && !count)) return;
    if (!pendingInverse && restorePage()) return;
    inFlight = ++nextRequest; dirty = false;
    emit command({"preview-render", double(inFlight), currentPage, pixelWidth, double(engineRevision), double(sourceSequence)});
}
bool TexpressoPreview::matches(const QJsonObject &h) const
{
    return h["requestId"].toDouble() == inFlight && h["revision"].toDouble() == engineRevision
        && h["sequence"].toDouble() == sourceSequence && h["page"].toInt() == currentPage;
}
void TexpressoPreview::receive(const QJsonObject &header, const QByteArray &bytes)
{
    if (!matches(header)) return;
    Frame frame{header, bytes};
    if (decoding) pending = std::move(frame); else decode(std::move(frame));
}
void TexpressoPreview::decode(Frame frame)
{
    decoding = true;
    auto watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, header = frame.header] {
        const auto image = watcher->result(); watcher->deleteLater(); decoding = false;
        if (matches(header)) {
            inFlight = 0;
            if (image.isNull()) emit failed(tr("Cannot decode TeXpresso preview image."));
            else {
                pixels = image;
                const auto b = header["bounds"].toArray();
                pageBounds = QRectF(QPointF(b[0].toDouble(), b[1].toDouble()), QPointF(b[2].toDouble(), b[3].toDouble()));
                needsNativePage = false; fresh = true; cachePage();
                finishInverse(); emit updated();
            }
        }
        if (pending) {
            auto next = std::move(*pending); pending.reset();
            if (matches(next.header)) decode(std::move(next));
        }
        request();
    });
    watcher->setFuture(QtConcurrent::run([frame = std::move(frame)]() mutable {
        if (frame.header["mime"] != "image/png" || frame.header.contains("patch")) return QImage();
        QBuffer buffer(&frame.bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "png");
        // Check the encoded dimensions too, before allowing decoder allocation.
        if (reader.size() != QSize(frame.header["width"].toInt(), frame.header["height"].toInt())) return QImage();
        return reader.read();
    }));
}
void TexpressoPreview::inverse(const QPointF &point)
{
    if (!fresh) return;
    pendingInverse = point;
    // A cached image does not select the engine's single hit-testing display
    // list. Prepare that page before using its source map, like the extension.
    if (needsNativePage) { dirty = true; request(); }
    else finishInverse();
}
void TexpressoPreview::finishInverse()
{
    if (!pendingInverse) return;
    const auto point = *pendingInverse; pendingInverse.reset();
    emit command({"preview-inverse", double(engineRevision), double(sourceSequence), currentPage, point.x(), point.y()});
}
void TexpressoPreview::clearCache()
{
    releaseCache(); needsNativePage = true;
}
void TexpressoPreview::releaseCache()
{
    pages.clear(); cacheBytes = 0; pendingInverse.reset();
}
void TexpressoPreview::cachePage()
{
    if (!active) return;
    constexpr qint64 budget = 32 * 1024 * 1024;
    for (int i = pages.size() - 1; i >= 0; --i) {
        if (pages[i].page == currentPage && pages[i].width == pixelWidth) {
            cacheBytes -= pages[i].image.sizeInBytes(); pages.removeAt(i);
        }
    }
    const auto bytes = pixels.sizeInBytes();
    if (bytes > budget) return;
    while (!pages.isEmpty() && (pages.size() >= 4 || cacheBytes + bytes > budget)) {
        cacheBytes -= pages.first().image.sizeInBytes(); pages.removeFirst();
    }
    pages.append({currentPage, pixelWidth, pixels, pageBounds}); cacheBytes += bytes;
}
bool TexpressoPreview::restorePage()
{
    for (int i = 0; i < pages.size(); ++i) {
        if (pages[i].page != currentPage || pages[i].width != pixelWidth) continue;
        const auto cached = pages.takeAt(i); pages.append(cached);
        pixels = cached.image; pageBounds = cached.bounds;
        dirty = false; fresh = true; needsNativePage = true;
        emit updated(); return true;
    }
    return false;
}
void TexpressoPreview::forward(const QString &path, int zeroBasedLine)
{
    if (active) {
        // Forward search can move the native viewer before its reply arrives.
        needsNativePage = true; waitingForward = true;
        emit command({"synctex-forward", path, zeroBasedLine + 1});
    }
}
