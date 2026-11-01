#pragma once

#include <QString>

// Keep stable dictionary paths in settings; materialize only when loading.
namespace DictionaryArchive {
QString logicalPath(const QString &path);
QString locate(const QString &path);
bool exists(const QString &path);
bool isCompressed(const QString &path);
QString materialize(const QString &path, QString *error = nullptr);
}
