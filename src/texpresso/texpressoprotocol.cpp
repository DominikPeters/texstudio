#include "texpressoprotocol.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>

namespace {
bool count(const QJsonValue &v) {
    const double n = v.toDouble(-1);
    return v.isDouble() && n >= 0 && n <= 2147483647 && std::floor(n) == n;
}
bool coordinate(const QJsonValue &v) {
    return v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) < 1e9;
}
bool name(const QJsonValue &v) {
    if (!v.isString() || v.toString().isEmpty() || v.toString().size() > 4096) return false;
    for (const auto c : v.toString()) if (c.unicode() < 32 || c.unicode() == 127) return false;
    return true;
}
bool annotations(const QJsonValue &v) {
    if (!v.isObject()) return false;
    const auto object = v.toObject();
    if (!object["links"].isArray() || !object["destinations"].isArray()) return false;
    const auto links = object["links"].toArray(), destinations = object["destinations"].toArray();
    if (links.size() > 4096 || destinations.size() > 4096) return false;
    for (const auto &link : links) {
        if (!link.isObject()) return false;
        const auto l = link.toObject(); const auto b = l["bounds"].toArray();
        if (b.size() != 4 || !name(l["uri"])) return false;
        for (const auto &c : b) if (!coordinate(c)) return false;
        if (b[2].toDouble() <= b[0].toDouble() || b[3].toDouble() <= b[1].toDouble()) return false;
    }
    for (const auto &dest : destinations) {
        if (!dest.isObject()) return false;
        const auto d = dest.toObject();
        if (!name(d["name"]) || !coordinate(d["x"]) || !coordinate(d["y"])) return false;
    }
    return true;
}
}

bool TexpressoProtocol::validImage(const QJsonObject &h)
{
    if (h.contains("annotations") && !annotations(h["annotations"])) return false;
    for (const char *key : {"requestId", "revision", "sequence", "page", "width", "height", "byteLength"})
        if (!count(h.value(QLatin1String(key)))) return false;
    const qint64 w = h["width"].toInt(), height = h["height"].toInt();
    if (!w || !height || w > 8192 || height > 8192 || w * height > 4000000
        || h["byteLength"].toInt() > 16 * 1024 * 1024) return false;
    if (h["mime"] != "image/png" && h["mime"] != "image/svg+xml") return false;
    if (h.contains("rasterId") && (!count(h["rasterId"]) || h["rasterId"].toInt() < 1)) return false;
    const auto bounds = h["bounds"].toArray();
    if (bounds.size() != 4) return false;
    for (const auto &v : bounds)
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || std::abs(v.toDouble()) >= 1e9) return false;
    if (bounds[2].toDouble() <= bounds[0].toDouble() || bounds[3].toDouble() <= bounds[1].toDouble()) return false;
    if (h.contains("patch")) {
        const auto patch = h["patch"].toObject();
        const auto rect = patch["rect"].toArray();
        if (h["mime"] != "image/png" || !count(h["rasterId"]) || h["rasterId"].toInt() < 1
            || !count(patch["base"]) || patch["base"].toInt() < 1 || rect.size() != 4) return false;
        for (const auto &v : rect) if (!count(v)) return false;
        const qint64 x = rect[0].toInt(), y = rect[1].toInt(), pw = rect[2].toInt(), ph = rect[3].toInt();
        return h["byteLength"].toInt() == 0 ? x == 0 && y == 0 && pw == 0 && ph == 0
            : pw > 0 && ph > 0 && x + pw <= w && y + ph <= height;
    }
    return h["byteLength"].toInt() > 0;
}

void TexpressoProtocol::feed(const QByteArray &bytes)
{
    if (broken) return;
    pending += bytes;
    while (!broken) {
        if (bodyLength >= 0) {
            if (pending.size() < bodyLength) return;
            const auto data = pending.left(bodyLength);
            pending.remove(0, bodyLength);
            const auto metadata = header;
            header = {};
            bodyLength = -1;
            emit image(metadata, data);
        } else {
            const auto end = pending.indexOf('\n');
            if (end < 0) {
                if (pending.size() > 1024 * 1024) fail(tr("TeXpresso protocol line exceeds its limit."));
                return;
            }
            if (end > 1024 * 1024) { fail(tr("TeXpresso protocol line exceeds its limit.")); return; }
            const auto line = pending.left(end);
            pending.remove(0, end + 1);
            if (line.trimmed().isEmpty()) continue;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            const auto value = document.array();
            if (error.error != QJsonParseError::NoError || !document.isArray() || value.isEmpty() || !value[0].isString()) {
                fail(tr("Invalid TeXpresso protocol message.")); return;
            }
            if (value[0] == "preview-image") {
                if (value.size() != 2 || !value[1].isObject() || !validImage(value[1].toObject())) {
                    fail(tr("Invalid TeXpresso image header.")); return;
                }
                header = value[1].toObject();
                bodyLength = header["byteLength"].toInt();
            } else emit message(value);
        }
    }
}

void TexpressoProtocol::finish()
{
    if (bodyLength >= 0 || !pending.trimmed().isEmpty()) fail(tr("TeXpresso exited with an incomplete protocol frame."));
}
void TexpressoProtocol::reset() { pending.clear(); header = {}; bodyLength = -1; broken = false; }
void TexpressoProtocol::fail(const QString &reason)
{
    broken = true; pending.clear(); header = {}; bodyLength = -1;
    emit failed(reason);
}
