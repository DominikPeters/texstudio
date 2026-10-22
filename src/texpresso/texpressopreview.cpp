#include "texpressopreview.h"
#include "texpressoprotocol.h"
#include "ViewerLinks.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>

namespace {
QRectF frameBounds(const QJsonObject &header)
{
    const auto b = header["bounds"].toArray();
    return QRectF(QPointF(b[0].toDouble(), b[1].toDouble()), QPointF(b[2].toDouble(), b[3].toDouble()));
}
}
TexpressoPreview::TexpressoPreview(QObject *parent) : QObject(parent)
{
    destinationTimer.setSingleShot(true); destinationTimer.setInterval(30000);
    connect(&destinationTimer, &QTimer::timeout, this, [this] {
        if (!destination) return;
        cancelDestination();
        emit failed(tr("The link target did not become available. Try the link again after compilation finishes."));
        request();
    });
    forwardTimer.setSingleShot(true); forwardTimer.setInterval(2000);
    connect(&forwardTimer, &QTimer::timeout, this, [this] {
        waitingForward = false;
        // Definitions-only inputs and missing source records can finish without
        // a forward reply. Release speculation once this pass is known complete,
        // while keeping late valid replies eligible until manual navigation.
        if (finished && stateRevision >= 0 && stateRevision == engineRevision) nativeForwardPending = false;
        emit activityChanged(); request(); schedulePrefetch();
    });
    prefetchTimer.setSingleShot(true); prefetchTimer.setInterval(150);
    connect(&prefetchTimer, &QTimer::timeout, this, [this] { prefetchReady = true; request(); });
    gestureTimer.setSingleShot(true); gestureTimer.setInterval(150);
    connect(&gestureTimer, &QTimer::timeout, this, [this] {
        gestureActive = false; emit activityChanged(); request(); schedulePrefetch();
    });
}
bool TexpressoPreview::busy() const
{
    if (!active) return false;
    if (sourceTarget || destination || gestureActive || (inFlight && (!renderingBackground || visiblePages.contains(flightPage))) || dirty || pendingRegion || pendingInverse || waitingForward) return true;
    for (int page : visiblePages) if (!pageValid(page) && !prefetched.contains(page)) return true;
    return false;
}
void TexpressoPreview::setPreparationPending(bool value)
{
    preparationPending = value;
    if (value) { prefetchTimer.stop(); prefetchReady = false; interruptBackground(); }
    else { request(); schedulePrefetch(); }
}
void TexpressoPreview::interruptBackground()
{
    if (!inFlight || !renderingBackground) return;
    prefetched.remove(flightPage);
    emit command({"preview-cancel", double(inFlight), currentPage});
    inFlight = 0; pending.reset(); renderingBackground = false;
}
QImage TexpressoPreview::pageImage(int page) const
{
    if (page == currentPage && fresh) return pixels;
    for (const auto &cached : pages) if (cached.page == page && cached.width == pixelWidth) return cached.image;
    // The retained foreground frame can outlive its cache entry, but it still
    // belongs to the page that produced it, even after selection moves ahead.
    return page == displayedPage ? pixels : QImage();
}
QRectF TexpressoPreview::boundsForPage(int page) const
{
    if (page == currentPage && fresh) return pageBounds;
    for (const auto &cached : pages) if (cached.page == page) return cached.bounds;
    if (page == displayedPage) return pageBounds;
    return pageBounds.isEmpty() ? QRectF(0, 0, 595, 842) : pageBounds;
}
bool TexpressoPreview::pageValid(int page) const
{
    if (page == currentPage && fresh) return true;
    for (const auto &cached : pages) if (cached.page == page && cached.width == pixelWidth) return true;
    return false;
}
QJsonObject TexpressoPreview::annotationsForPage(int page) const
{
    if (!supportsLinks || !pageValid(page)) return {};
    if (page == currentPage && fresh) return pageAnnotations;
    for (const auto &cached : pages) if (cached.page == page && cached.width == pixelWidth) return cached.annotations;
    return {};
}
void TexpressoPreview::activateLink(int page, int index, qint64 revision, qint64 sequence)
{
    if (!active || !supportsLinks || revision != engineRevision || sequence != sourceSequence || index < 0) return;
    const auto links = annotationsForPage(page)["links"].toArray();
    if (index >= links.size()) return;
    const auto uri = links[index].toObject()["uri"].toString();
    cancelForward(page);
    if (!uri.startsWith('#')) {
        const auto url = ViewerLinks::externalUrl(uri);
        if (url) { cancelDestination(); emit interactionStarted(); emit externalLinkRequested(*url); }
        return;
    }
    if (uri.size() < 2) return;
    emit interactionStarted(); interruptBackground(); cancelRegion();
    pendingInverse.reset(); waitingForward = false; forwardTimer.stop();
    destination = Destination{uri.mid(1), engineRevision, sourceSequence};
    needsNativePage = true;
    destinationTimer.start();
    emit command({"preview-destination", double(engineRevision), double(sourceSequence), destination->name});
    emit activityChanged();
}
void TexpressoPreview::cancelDestination(bool notifyEngine)
{
    destinationTimer.stop();
    if (!destination) return;
    const auto old = *destination; destination.reset();
    // A same-sequence barrier clears the native destination demand without
    // changing the document. Abandoned links must not keep compiling ahead.
    if (notifyEngine && old.page < 0 && old.revision == engineRevision && old.sequence == sourceSequence)
        emit command({"preview-barrier", double(sourceSequence)});
    emit activityChanged();
}
void TexpressoPreview::finishDestination()
{
    if (!destination || destination->page != currentPage || !fresh) return;
    const auto name = destination->name;
    const auto locations = pageAnnotations["destinations"].toArray();
    for (const auto &item : locations) {
        const auto d = item.toObject();
        if (d["name"].toString() != name) continue;
        const auto point = QPointF(d["x"].toDouble(), d["y"].toDouble());
        cancelDestination(); emit reveal(point); return;
    }
    cancelDestination();
    emit failed(tr("This link target has no position in the rendered page."));
}
void TexpressoPreview::setViewportPages(const QList<int> &visible, bool gesture)
{
    QList<int> next;
    for (int page : visible) if (page >= 0 && page <= 100000 && (!finished || page < count) && !next.contains(page) && next.size() < 16) next.append(page);
    if (!visible.isEmpty() && next.isEmpty()) return;
    if (gesture) { cancelDestination(); cancelForward(next.isEmpty() ? currentPage : next.first()); }
    if (next == visiblePages && !gesture) return;
    if (gesture) {
        emit interactionStarted(); gestureActive = true; gestureTimer.start();
        frontierBudget = 1; prefetched.clear(); prefetchReady = false; prefetchTimer.stop();
    }
    visiblePages = next;
    // Layout refreshes can still describe the old viewport while a link is
    // resolving. Only a user gesture may replace that explicit navigation.
    if (!next.isEmpty() && currentPage != next.first() && !destination && !followingSource() && (gesture || !next.contains(currentPage))) {
        const auto position = scroll; setPage(next.first()); scroll = position;
    }
    // A scrollbar produces many updates while the same neighbor remains on
    // screen. Cancelling its export on every tick can keep it white throughout
    // an entire gesture. Only obsolete speculative work needs to yield here.
    if (renderingBackground && !visiblePages.contains(flightPage)) interruptBackground();
    request(); emit activityChanged(); schedulePrefetch();
}
void TexpressoPreview::schedulePrefetch()
{
    if (active && supportsBackground && !gestureActive && !preparationPending && !visiblePages.isEmpty()
        && !prefetchReady && !prefetchTimer.isActive() && !busy()) prefetchTimer.start();
}
void TexpressoPreview::setCapabilities(const QJsonObject &capabilities)
{
    supportsLinks = capabilities["links"].toBool();
    if (!supportsLinks) cancelDestination();
    supportsBackground = capabilities["backgroundRendering"].toBool();
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
    emit interactionStarted(); interruptBackground();
    regionToken = token;
    pendingRegion = Region{clipped, std::clamp(width, 1, 4096), token};
    request();
}
void TexpressoPreview::cancelRegion()
{
    pendingRegion.reset(); regionToken = -1; emit activityChanged();
}

void TexpressoPreview::sourceChanged(qint64 sequence)
{
    cancelDestination(false);
    emit interactionStarted();
    prefetched.clear(); frontierBudget = 1; prefetchReady = false; prefetchTimer.stop();
    sourceSequence = sequence; engineRevision = stateRevision = -1;
    dirty = true; fresh = false; inFlight = 0; pending.reset();
    waitingForward = false; nativeForwardPending = false; sourceFollowing = false; sourceTarget.reset(); forwardTimer.stop();
    cancelRegion();
    clearCache();
    emit updated();
}
void TexpressoPreview::setActive(bool enabled)
{
    active = enabled;
    if (!active) cancelDestination();
    if (!active) { cancelRegion(); prefetchTimer.stop(); gestureTimer.stop(); gestureActive = false; interruptBackground(); }
    if (active) request();
    emit activityChanged();
}
void TexpressoPreview::setPage(int page)
{
    if (!settingDestinationPage) cancelDestination();
    pendingInverse.reset();
    const int maximum = finished ? std::max(0, count - 1) : 100000;
    page = std::clamp(page, 0, maximum);
    if (page == currentPage) return;
    emit interactionStarted(); interruptBackground();
    cancelRegion(); rasterId = 0;
    currentPage = page; dirty = true; fresh = false; inFlight = 0; pending.reset();
    scroll = {}; emit updated(); request();
}
void TexpressoPreview::setWidth(int width)
{
    width = std::clamp(width, 1, 4096);
    if (width == pixelWidth) return;
    emit interactionStarted(); interruptBackground();
    pixelWidth = width; prefetched.clear(); dirty = true; inFlight = 0;
    cancelRegion(); rasterId = 0;
    request();
}
void TexpressoPreview::message(const QJsonArray &m)
{
    const auto verb = m[0].toString();
    if (verb == "preview-state" && m.size() == 6 && m[2].toDouble() == sourceSequence) {
        const auto revision = qint64(m[1].toDouble());
        if (revision < engineRevision) return;
        if (revision != engineRevision) { sourceTarget.reset(); cancelDestination(false); inFlight = 0; dirty = true; fresh = false; cancelRegion(); clearCache(); }
        engineRevision = stateRevision = revision;
        if (m[5].toInt() != currentPage) needsNativePage = true;
        count = std::max(0, m[3].toInt()); finished = m[4].toBool();
        if (finished && !forwardTimer.isActive()) nativeForwardPending = false;
        if (destination) destinationTimer.start();
        if (finished && count && currentPage >= count) setPage(count - 1);
        // No render request is sent for a terminal zero-page document. Report
        // that terminal state here; waiting for preview-error would leave the
        // UI updating forever because the engine only emits it for a request.
        if (finished && !count) {
            inFlight = 0; fresh = false; pending.reset(); pendingInverse.reset(); cancelRegion();
            emit failed(tr("The document has not produced a page. Open Output for compiler messages."));
        }
        emit updated(); request();
    } else if (verb == "preview-invalidated" && m.size() == 3 && m[2].toDouble() == sourceSequence) {
        if (qint64(m[1].toDouble()) < engineRevision) return;
        cancelDestination(false);
        sourceTarget.reset();
        clearCache();
        cancelRegion();
        engineRevision = qint64(m[1].toDouble()); dirty = true; fresh = false; inFlight = 0;
        emit updated(); request();
    } else if (verb == "preview-retry" && m.size() == 2 && m[1].toDouble() == inFlight) {
        inFlight = 0;
        if (!renderingRegion && !renderingBackground) dirty = true;
        request();
    } else if (verb == "preview-error" && m.size() == 3 && m[1].toDouble() == inFlight) {
        inFlight = 0;
        if (!renderingRegion && !renderingBackground) { cancelDestination(); pendingInverse.reset(); emit failed(m[2].toString()); }
        request();
    } else if (verb == "preview-destination" && destination && m.size() == 5
        && m[1].isDouble() && m[1].toDouble() == destination->revision
        && m[2].isDouble() && m[2].toDouble() == destination->sequence
        && m[3].isString() && m[3].toString() == destination->name && m[4].isDouble()) {
        const double target = m[4].toDouble();
        if (!std::isfinite(target) || std::floor(target) != target || target < -1 || target > 100000) return;
        if (target < 0) {
            cancelDestination(false); emit failed(tr("This link target is not available in the document.")); request();
        } else {
            destination->page = int(target); destinationTimer.start();
            QScopedValueRollback<bool> guard(settingDestinationPage, true);
            setPage(int(target));
            finishDestination(); request();
        }
    } else if (verb == "preview-forward" && sourceFollowing && m.size() == 6 && m[1].toDouble() == engineRevision && m[2].toDouble() == sourceSequence) {
        const int page = m[3].toInt(-1);
        if (page < 0 || page > 100000 || !std::isfinite(m[4].toDouble()) || !std::isfinite(m[5].toDouble())) return;
        sourceTarget = SourceTarget{engineRevision, sourceSequence, page, {m[4].toDouble(), m[5].toDouble()}};
        waitingForward = false; nativeForwardPending = false; forwardTimer.stop();
        setPage(page); finishForward(); request();
    }
}
void TexpressoPreview::request()
{
    if (pumping) return;
    QScopedValueRollback<bool> guard(pumping, true);
    if ((!active && !pendingInverse) || inFlight || engineRevision < 0 || (finished && !count)) { emit activityChanged(); return; }
    if (destination && destination->page < 0) { emit activityChanged(); return; }
    if (dirty && !pendingInverse) restorePage();
    finishDestination(); finishForward();
    renderingBackground = false; flightPage = currentPage; flightWidth = pixelWidth;
    if (dirty) {
        emit interactionStarted();
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
    } else if (supportsBackground && !preparationPending && !destination && !followingSource()) {
        int target = -1;
        // Visible pages precede speculative neighbors. Never let an idle
        // frontier request recursively compile the entire document.
        for (int page : visiblePages) if (!pageValid(page) && !prefetched.contains(page)) { target = page; break; }
        if (target < 0 && prefetchReady && !gestureActive && !visiblePages.isEmpty()) {
            const int first = *std::min_element(visiblePages.begin(), visiblePages.end());
            const int last = *std::max_element(visiblePages.begin(), visiblePages.end());
            for (int page : {last + 1, first - 1}) {
                if (page < 0 || (finished && page >= count) || page > count || pageValid(page) || prefetched.contains(page)) continue;
                if (page >= count) { if (!frontierBudget) continue; --frontierBudget; }
                target = page; prefetched.insert(page); break;
            }
        }
        if (target >= 0) {
            if (visiblePages.contains(target)) emit interactionStarted();
            prefetched.insert(target);
            renderingBackground = true; renderingRegion = false; flightPage = target;
            inFlight = ++nextRequest;
            emit command({"preview-background", double(inFlight), target, pixelWidth, double(engineRevision), double(sourceSequence), 0, 0});
        }
    }
    emit activityChanged();
    if (!inFlight) schedulePrefetch();
}
bool TexpressoPreview::matches(const QJsonObject &h) const
{
    return h["requestId"].toDouble() == inFlight && h["revision"].toDouble() == engineRevision
        && h["sequence"].toDouble() == sourceSequence && h["page"].toInt() == flightPage;
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
    Frame frame{header, bytes, base, renderingRegion, flightRegionToken, renderingBackground};
    if (decoding) pending = std::move(frame); else decode(std::move(frame));
}
void TexpressoPreview::decode(Frame frame)
{
    decoding = true;
    auto watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, header = frame.header, region = frame.region, token = frame.token, background = frame.background] {
        const auto image = watcher->result(); watcher->deleteLater(); decoding = false;
        if (matches(header)) {
            inFlight = 0;
            if (region) {
                // A lens result must never replace the page or its patch base.
                if (!image.isNull() && token == regionToken && active && fresh)
                    emit regionReady(token, image, frameBounds(header));
            } else if (background) {
                if (!image.isNull()) { storePage(header["page"].toInt(), flightWidth, image, frameBounds(header), header["annotations"].toObject()); emit updated(); }
            } else if (image.isNull() && header.contains("patch")) retryFullFrame();
            else if (image.isNull()) emit failed(tr("Cannot decode TeXpresso preview image."));
            else {
                pixels = image; displayedPage = currentPage;
                pageBounds = frameBounds(header);
                pageAnnotations = header["annotations"].toObject();
                rasterId = supportsPatches ? qint64(header["rasterId"].toDouble()) : 0;
                rasterPage = currentPage; rasterBounds = pageBounds; rasterSize = pixels.size();
                needsNativePage = false; fresh = true; cachePage();
                finishInverse(); emit updated(); finishDestination(); finishForward();
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
    cancelDestination(); cancelForward(currentPage);
    emit interactionStarted(); interruptBackground();
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
    releaseCache();
    // Auxiliary reruns can advance only the engine revision. Attempted exports
    // belong to that revision just as much as their cached page images do.
    prefetched.clear(); prefetchReady = false; prefetchTimer.stop(); needsNativePage = true;
}
void TexpressoPreview::releaseCache()
{
    pages.clear(); cacheBytes = 0; pendingInverse.reset();
}
void TexpressoPreview::cachePage()
{
    storePage(currentPage, pixelWidth, pixels, pageBounds, pageAnnotations);
}
void TexpressoPreview::storePage(int page, int width, const QImage &image, const QRectF &bounds, const QJsonObject &annotations)
{
    if (!active) return;
    constexpr qint64 budget = 32 * 1024 * 1024;
    for (int i = pages.size() - 1; i >= 0; --i) {
        if (pages[i].page == page && pages[i].width == width) {
            cacheBytes -= pages[i].image.sizeInBytes(); pages.removeAt(i);
        }
    }
    const auto bytes = image.sizeInBytes();
    if (bytes > budget) return;
    const int entryLimit = std::clamp(int(visiblePages.size()) + 2, 4, 16);
    while (!pages.isEmpty() && (pages.size() >= entryLimit || cacheBytes + bytes > budget)) {
        int victim = -1;
        for (int i = 0; i < pages.size(); ++i) if (!visiblePages.contains(pages[i].page)) { victim = i; break; }
        // Speculative neighbors may not evict a page currently on screen.
        if (victim < 0) { if (!visiblePages.contains(page)) return; victim = 0; }
        cacheBytes -= pages[victim].image.sizeInBytes(); pages.removeAt(victim);
    }
    pages.append({page, width, image, bounds, annotations}); cacheBytes += bytes;
}
bool TexpressoPreview::restorePage()
{
    for (int i = 0; i < pages.size(); ++i) {
        if (pages[i].page != currentPage || pages[i].width != pixelWidth) continue;
        const auto cached = pages.takeAt(i); pages.append(cached);
        pixels = cached.image; displayedPage = currentPage; pageBounds = cached.bounds;
        pageAnnotations = cached.annotations;
        rasterId = 0;
        dirty = false; fresh = true; needsNativePage = true;
        emit updated(); return true;
    }
    return false;
}
void TexpressoPreview::forward(const QString &path, int zeroBasedLine)
{
    if (active) {
        cancelDestination();
        // Forward search can move the native viewer before its reply arrives.
        emit interactionStarted(); interruptBackground();
        sourceTarget.reset(); sourceFollowing = true; nativeForwardPending = true;
        needsNativePage = true; waitingForward = true; forwardTimer.start();
        emit command({"synctex-forward", path, zeroBasedLine + 1});
    }
}

void TexpressoPreview::finishForward()
{
    if (!sourceTarget || !fresh || sourceTarget->page != currentPage) return;
    const auto target = *sourceTarget;
    sourceTarget.reset();
    if (target.revision != engineRevision || target.sequence != sourceSequence) return;
    QScopedValueRollback<bool> guard(sourceReveal, true);
    emit reveal(target.point);
}
void TexpressoPreview::cancelForward(int page)
{
    emit manualNavigated();
    sourceTarget.reset(); waitingForward = false; nativeForwardPending = false; forwardTimer.stop();
    if (!sourceFollowing) return;
    sourceFollowing = false;
    // Cancel-all also retires a native foreground export without a reply.
    // Release its client credit before requesting the user's selected page.
    emit interactionStarted();
    emit command({"preview-cancel", 0, page});
    if (inFlight && !renderingBackground && !renderingRegion) dirty = true;
    if (renderingBackground) prefetched.remove(flightPage);
    inFlight = 0; pending.reset(); renderingBackground = false;
    cancelRegion(); needsNativePage = true;
    emit activityChanged();
}
void TexpressoPreview::navigateTo(int page)
{
    page = std::clamp(page, 0, finished ? std::max(0, count - 1) : 100000);
    cancelForward(page);
    setPage(page); request();
}
