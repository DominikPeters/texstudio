#ifndef TEXPRESSOPROVIDER_H
#define TEXPRESSOPROVIDER_H
#include <QProcessEnvironment>
#include <QJsonObject>
#include <QStringList>

// Provider choice is resolved once at session startup, never per keystroke.
namespace TexpressoProvider {
QString resolve(const QString &requested, const QProcessEnvironment &environment);
QString shellSetup(const QString &requested);
QStringList engineArguments(const QString &provider, bool installPackages, const QJsonObject &capabilities);
}
#endif
