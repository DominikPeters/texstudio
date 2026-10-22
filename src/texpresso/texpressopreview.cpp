#include "texpressopreview.h"
#include "texpressoprotocol.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
QRectF frameBounds(const QJsonObject &header)
{
    const auto b = header["bounds"].toArray();
    return QRectF(QPointF(b[0].toDouble(), b[1].toDouble()), QPointF(b[2].toDouble(), b[3].toDouble()));
}
}
void TexpressoPreview::setCapabilities(const QJsonObject &capabilities)
{
    supportsRegions = capabilities["regions"].toBool();
    supportsPatches = capabilities["pngPatches"].toBool();
    if (!supportsRegions) cancelRegion();
    if (!supportsPatches) rasterId = 0;
}
void TexpressoPreview::requestRegion(const QRectF &bounds, int width, qint64 token)
{
    if (!supportsRegions || !active || !fresh || bounds.isEmpty() || width < 1) return;
    const auto clipped = bounds.intersected(pageBounds);
    if (clipped.isEmpty() || !std::isfinite(clipped.x()) || !std::isfinite(clipped.y())
        || !std::isfinite(clipped.width()) || !std::isfinite(clipped.height())) return;
    regionToken = token;
    pendingRegion = Region{clipped, std::clamp(width, 1, 4096), token};
    request();
}
void TexpressoPreview::cancelRegion()
{
    pendingRegion.reset(); regionToken = -1;
}

void TexpressoPreview::sourceChanged(qint64 sequence)
{
    sourceSequence = sequence; engineRevision = -1;
    dirty = true; fresh = false; inFlight = 0; pending.reset();
    waitingForward = false;
    cancelRegion();
    clearCache();
    emit updated();
}
void TexpressoPreview::setActive(bool enabled)
{
    active = enabled;
    if (!active) cancelRegion();
    if (active) request();
}
void TexpressoPreview::setPage(int page)
{
    waitingForward = false;
    pendingInverse.reset();
    const int maximum = finished ? std::max(0, count - 1) : 100000;
    page = std::clamp(page, 0, maximum);
    if (page == currentPage) return;
    cancelRegion(); rasterId = 0;
    currentPage = page; dirty = true; fresh = false; inFlight = 0; pending.reset();
    scroll = {}; emit updated(); request();
}
void TexpressoPreview::setWidth(int width)
{
    width = std::clamp(width, 1, 4096);
    if (width == pixelWidth) return;
    pixelWidth = width; dirty = true; inFlight = 0;
    cancelRegion(); rasterId = 0;
    request();
}
void TexpressoPreview::message(const QJsonArray &m)
{
    const auto verb = m[0].toString();
    if (verb == "preview-state" && m.size() == 6 && m[2].toDouble() == sourceSequence) {
        const auto revision = qint64(m[1].toDouble());
        if (revision != engineRevision) { inFlight = 0; dirty = true; fresh = false; cancelRegion(); clearCache(); }
        engineRevision = revision;
        if (m[5].toInt() != currentPage) needsNativePage = true;
        count = std::max(0, m[3].toInt()); finished = m[4].toBool();
        if (finished && count && currentPage >= count) setPage(count - 1);
        emit updated(); request();
    } else if (verb == "preview-invalidated" && m.size() == 3 && m[2].toDouble() == sourceSequence) {
        clearCache();
        cancelRegion();
        engineRevision = qint64(m[1].toDouble()); dirty = true; fresh = false; inFlight = 0;
        emit updated(); request();
    } else if (verb == "preview-retry" && m.size() == 2 && m[1].toDouble() == inFlight) {
        inFlight = 0;
        if (!renderingRegion) dirty = true;
        request();
    } else if (verb == "preview-error" && m.size() == 3 && m[1].toDouble() == inFlight) {
        inFlight = 0;
        if (!renderingRegion) { pendingInverse.reset(); emit failed(m[2].toString()); }
        request();
    } else if (verb == "preview-forward" && waitingForward && m.size() == 6 && m[1].toDouble() == engineRevision && m[2].toDouble() == sourceSequence) {
        setPage(m[3].toInt()); emit reveal(QPointF(m[4].toDouble(), m[5].toDouble()));
    }
}
void TexpressoPreview::request()
{
    if ((!active && !pendingInverse) || inFlight || engineRevision < 0 || (finished && !count)) return;
    if (dirty && !pendingInverse) restorePage();
    if (dirty) {
        inFlight = ++nextRequest; dirty = false; renderingRegion = false;
        if (supportsPatches)
            emit command({"preview-png", double(inFlight), currentPage, pixelWidth, double(engineRevision), double(sourceSequence), double(rasterId)});
        else emit command({"preview-render", double(inFlight), currentPage, pixelWidth, double(engineRevision), double(sourceSequence)});
    } else if (pendingRegion && fresh) {
        const auto region = *pendingRegion; pendingRegion.reset();
        inFlight = ++nextRequest; renderingRegion = true; flightRegionToken = region.token;
        const auto b = region.bounds;
        emit command({"preview-region", double(inFlight), currentPage, region.width, double(engineRevision), double(sourceSequence),
            b.left(), b.top(), b.right(), b.bottom()});
    }
}
bool TexpressoPreview::matches(const QJsonObject &h) const
{
    return h["requestId"].toDouble() == inFlight && h["revision"].toDouble() == engineRevision
        && h["sequence"].toDouble() == sourceSequence && h["page"].toInt() == currentPage;
}
void TexpressoPreview::receive(const QJsonObject &header, const QByteArray &bytes)
{
    if (!matches(header)) return;
    if (!TexpressoProtocol::validImage(header) || header["byteLength"].toInt() != bytes.size()) {
        inFlight = 0; emit failed(tr("Invalid TeXpresso preview image.")); return;
    }
    QImage base;
    if (header.contains("patch")) {
        const auto patch = header["patch"].toObject();
        if (renderingRegion || !supportsPatches || !rasterId || patch["base"].toDouble() != rasterId
            || rasterPage != currentPage || QSize(header["width"].toInt(), header["height"].toInt()) != rasterSize
            || frameBounds(header) != rasterBounds || pixels.size() != rasterSize) {
            retryFullFrame(); return;
        }
        base = pixels;
    }
    Frame frame{header, bytes, base, renderingRegion, flightRegionToken};
    if (decoding) pending = std::move(frame); else decode(std::move(frame));
}
void TexpressoPreview::decode(Frame frame)
{
    decoding = true;
    auto watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, header = frame.header, region = frame.region, token = frame.token] {
        const auto image = watcher->result(); watcher->deleteLater(); decoding = false;
        if (matches(header)) {
            inFlight = 0;
            if (region) {
                // A lens result must never replace the page or its patch base.
                if (!image.isNull() && token == regionToken && active && fresh)
                    emit regionReady(token, image, frameBounds(header));
            } else if (image.isNull() && header.contains("patch")) retryFullFrame();
            else if (image.isNull()) emit failed(tr("Cannot decode TeXpresso preview image."));
            else {
                pixels = image;
                pageBounds = frameBounds(header);
                rasterId = supportsPatches ? qint64(header["rasterId"].toDouble()) : 0;
                rasterPage = currentPage; rasterBounds = pageBounds; rasterSize = pixels.size();
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
        if (frame.header["mime"] != "image/png") return QImage();
        const bool patch = frame.header.contains("patch");
        if (patch && frame.bytes.isEmpty()) return frame.base;
        QBuffer buffer(&frame.bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "png");
        // Check the encoded dimensions too, before allowing decoder allocation.
        const auto r = frame.header["patch"].toObject()["rect"].toArray();
        const QSize size = patch ? QSize(r[2].toInt(), r[3].toInt())
            : QSize(frame.header["width"].toInt(), frame.header["height"].toInt());
        if (reader.size() != size) return QImage();
        const auto image = reader.read();
        if (!patch || image.isNull()) return image;
        // Detach only in the worker; a previous paint/cache can still use the
        // immutable base. Source composition also replaces transparent pixels.
        QImage composed = frame.base.copy();
        QPainter painter(&composed); painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(r[0].toInt(), r[1].toInt(), image); painter.end();
        return composed;
    }));
}
void TexpressoPreview::retryFullFrame()
{
    rasterId = 0; inFlight = 0; dirty = true; fresh = false; pending.reset();
    // Bypass a cached page: it cannot repair the engine's rejected base.
    const auto inverse = pendingInverse;
    clearCache(); pendingInverse = inverse; emit updated(); request();
}
void TexpressoPreview::inverse(const QPointF &point)
{
    if (!fresh) return;
    cancelRegion();
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
        rasterId = 0;
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
