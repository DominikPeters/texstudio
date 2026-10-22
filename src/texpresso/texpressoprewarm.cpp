#include "texpressoprewarm.h"
#include <utility>

TexpressoPrewarm::TexpressoPrewarm(std::function<qint64()> allocate, QObject *parent)
    : QObject(parent), nextId(std::move(allocate))
{
    cursorTimer.setSingleShot(true); cursorTimer.setInterval(200);
    watchdog.setSingleShot(true); watchdog.setInterval(5000);
    connect(&cursorTimer, &QTimer::timeout, this, &TexpressoPrewarm::prepare);
    connect(&watchdog, &QTimer::timeout, this, [this] { interrupt(false); emit settled(); });
}
void TexpressoPrewarm::setContext(Context next)
{
    const bool changedEpoch = next.revision != context.revision || next.sequence != context.sequence;
    context = next;
    if (!context.available || !context.idle || changedEpoch) interrupt();
    refresh();
}
void TexpressoPrewarm::select(const QString &identity, Resolver resolve)
{
    if (target && target->identity == identity) { refresh(); return; }
    interrupt();
    target.reset();
    if (!identity.isEmpty() && resolve) target = Target{identity, std::move(resolve), false, {}};
    refresh();
}
void TexpressoPrewarm::refresh(bool foregroundReady)
{
    if (foregroundReady) {
        for (const auto &key : skipped) attempts.removeAll(key);
        skipped.clear();
        if (target && !target->position) target->resolved = false;
    }
    if (pending() || !context.available || !context.idle || !target) return;
    if (target->resolved) {
        if (!target->position) return;
        const auto &p = *target->position;
        const QString key = QString::number(context.revision) + ':' + QString::number(context.sequence)
            + ':' + p.path + ':' + QString::number(p.byteOffset / 64);
        if (attempts.contains(key)) return;
    }
    cursorTimer.start(); emit pendingChanged(true);
}
void TexpressoPrewarm::remember(const QString &key)
{
    attempts.append(key);
    while (attempts.size() > 16) skipped.remove(attempts.takeFirst());
}
void TexpressoPrewarm::prepare()
{
    if (!context.available || !context.idle || !target) { emit pendingChanged(false); return; }
    if (!target->resolved) { target->position = target->resolve(); target->resolved = true; }
    if (!target->position) { emit pendingChanged(false); return; }
    const auto p = *target->position;
    const QString key = QString::number(context.revision) + ':' + QString::number(context.sequence)
        + ':' + p.path + ':' + QString::number(p.byteOffset / 64);
    if (p.path.isEmpty() || p.byteOffset < 0 || attempts.contains(key)) { emit pendingChanged(false); return; }
    remember(key);
    flight = Flight{nextId(), context.revision, context.sequence, key};
    const auto request = *flight;
    watchdog.start(); emit started();
    if (!flight || flight->id != request.id) return;
    emit command({"preview-prepare", double(request.id), p.path, p.byteOffset,
        double(request.revision), double(request.sequence)});
}
void TexpressoPrewarm::message(const QJsonArray &m)
{
    if (!flight || m.size() != 5 || m[0] != "preview-prepared"
        || m[1].toDouble(-1) != flight->id || m[2].toDouble(-1) != flight->revision
        || m[3].toDouble(-1) != flight->sequence) return;
    const auto status = m[4].toString();
    if (status != "ready" && status != "cached" && status != "skipped" && status != "cancelled") return;
    if (status == "skipped") skipped.insert(flight->key);
    watchdog.stop(); flight.reset(); emit pendingChanged(false); emit settled();
}
void TexpressoPrewarm::interrupt(bool retry)
{
    const bool wasPending = pending();
    cursorTimer.stop(); watchdog.stop();
    const auto active = flight; flight.reset();
    if (active) {
        if (retry) attempts.removeAll(active->key);
        emit command({"preview-prepare-cancel", double(active->id)});
    }
    if (wasPending) emit pendingChanged(false);
}
void TexpressoPrewarm::reset()
{
    interrupt(); attempts.clear(); skipped.clear();
    if (target) { target->resolved = false; target->position.reset(); }
}
