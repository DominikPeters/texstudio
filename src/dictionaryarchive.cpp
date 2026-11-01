#include "dictionaryarchive.h"
#include "quazip.h"
#include "quazipfile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
QByteArray digest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}
bool cached(const QString &directory, const QString &requested)
{
    QFile marker(directory + "/ready.json");
    if (!marker.open(QIODevice::ReadOnly)) return false;
    const auto files = QJsonDocument::fromJson(marker.readAll()).object();
    const QString companion = requested.chopped(4) + (requested.endsWith(".dic") ? ".aff" : ".idx");
    if (!files.contains(requested) || (requested.endsWith(".dic") && !files.contains(companion))) return false;
    for (auto it = files.begin(); it != files.end(); ++it) {
        if ((it.key() != requested && it.key() != companion)
            || digest(directory + '/' + it.key()) != it.value().toString().toLatin1()) return false;
    }
    return true;
}
}

QString DictionaryArchive::logicalPath(const QString &path)
{
    return path.endsWith(".dic.zip") || path.endsWith(".dat.zip") ? path.chopped(4) : path;
}

QString DictionaryArchive::locate(const QString &path)
{
    const QString logical = logicalPath(path);
    QFileInfo file(logical);
    if (file.isFile() && file.isReadable()) return file.canonicalFilePath();
    if (!logical.endsWith(".dic") && !logical.endsWith(".dat")) return {};
    QFileInfo archive(logical + ".zip");
    return archive.isFile() && archive.isReadable() ? archive.canonicalFilePath().chopped(4) : QString();
}

bool DictionaryArchive::exists(const QString &path) { return !locate(path).isEmpty(); }
bool DictionaryArchive::isCompressed(const QString &path)
{
    return !QFileInfo(logicalPath(path)).isFile() && exists(path);
}

QString DictionaryArchive::materialize(const QString &path, QString *error)
{
    if (error) error->clear();
    const auto fail = [&](const QString &message) { if (error) *error = message; return QString(); };
    const QString logical = locate(path);
    if (logical.isEmpty()) return fail(QObject::tr("Dictionary does not exist: %1").arg(path));
    if (QFileInfo(logical).isFile()) return logical;
    const QString archive = logical + ".zip";
    const auto hash = digest(archive);
    if (hash.isEmpty()) return fail(QObject::tr("Cannot read dictionary archive: %1").arg(archive));
    QString cache;
#ifndef NO_TESTS
    cache = qEnvironmentVariable("TEXSTUDIO_TEST_DICTIONARY_CACHE");
#endif
    if (cache.isEmpty()) cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty()) return fail(QObject::tr("No dictionary cache location is available."));
    const QString root = cache + "/dictionaries/v1";
    if (!QDir().mkpath(root)) return fail(QObject::tr("Cannot create dictionary cache: %1").arg(root));
    const QString directory = root + '/' + QString::fromLatin1(hash);
    const QString name = QFileInfo(logical).fileName();
    // Coordinate both other threads and other TeXstudio processes. Never expose
    // half an affix/dictionary pair, and repair caches removed or damaged by OS cleanup.
    QLockFile lock(directory + ".lock");
    if (!lock.tryLock(10000)) return fail(QObject::tr("Dictionary cache is busy: %1").arg(name));
    if (cached(directory, name)) return directory + '/' + name;
    QuaZip zip(archive);
    if (!zip.open(QuaZip::mdUnzip)) return fail(QObject::tr("Cannot open dictionary archive: %1").arg(archive));
    QTemporaryDir temporary(root + "/extract-XXXXXX");
    if (!temporary.isValid()) return fail(QObject::tr("Cannot create dictionary extraction directory."));
    QJsonObject hashes;
    const bool spelling = name.endsWith(".dic");
    const QString companion = name.chopped(4) + (spelling ? ".aff" : ".idx");
    if (!zip.goToFirstFile()) return fail(QObject::tr("Dictionary archive is empty: %1").arg(archive));
    do {
        QuaZipFileInfo64 info;
        if (!zip.getCurrentFileInfo(&info) || (info.name != name && info.name != companion)
            || hashes.contains(info.name) || info.uncompressedSize > 128 * 1024 * 1024)
            return fail(QObject::tr("Invalid dictionary archive entry in %1").arg(archive));
        QuaZipFile input(&zip);
        QFile output(temporary.filePath(info.name));
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            return fail(QObject::tr("Cannot extract dictionary: %1").arg(info.name));
        QCryptographicHash contents(QCryptographicHash::Sha256);
        qint64 written = 0;
        while (!input.atEnd()) {
            const auto block = input.read(65536);
            if (block.isEmpty() || output.write(block) != block.size())
                return fail(QObject::tr("Cannot extract dictionary: %1").arg(info.name));
            contents.addData(block); written += block.size();
            if (written > 128 * 1024 * 1024) return fail(QObject::tr("Dictionary entry is too large."));
        }
        input.close();
        if (input.getZipError() != UNZ_OK || written != qint64(info.uncompressedSize) || !output.flush())
            return fail(QObject::tr("Damaged dictionary archive: %1").arg(archive));
        output.close();
        hashes.insert(info.name, QString::fromLatin1(contents.result().toHex()));
    } while (zip.goToNextFile());
    if (zip.getZipError() != UNZ_OK || !hashes.contains(name) || (spelling && !hashes.contains(companion)))
        return fail(QObject::tr("Incomplete dictionary archive: %1").arg(archive));
    QFile marker(temporary.filePath("ready.json"));
    const auto metadata = QJsonDocument(hashes).toJson(QJsonDocument::Compact);
    if (!marker.open(QIODevice::WriteOnly) || marker.write(metadata) != metadata.size() || !marker.flush())
        return fail(QObject::tr("Cannot write dictionary cache metadata."));
    marker.close();
    if ((QFileInfo::exists(directory) && !QDir(directory).removeRecursively()) || !QDir().rename(temporary.path(), directory))
        return fail(QObject::tr("Cannot publish dictionary cache: %1").arg(directory));
    return directory + '/' + name;
}
