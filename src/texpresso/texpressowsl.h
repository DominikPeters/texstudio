#ifndef TEXPRESSOWSL_H
#define TEXPRESSOWSL_H
#include <QHash>
#include <QJsonArray>
#include <QStringList>

// Editor paths stay Windows paths in sessions. Only known protocol path fields
// cross this boundary; source text, URIs and binary image bodies are untouched.
class TexpressoWsl
{
public:
    bool enabled = false;
    QString distribution, error, home;
    QString requestedProvider = "auto", provider;
    qint64 freeBytes = -1;
    QHash<QString, QString> drives;
    void prepare(const QString &root);
    QString toEngine(QString path) const;
    QString toEditor(QString path) const;
    QString commandArgument(const QString &argument, const QString &workingDirectory) const;
    QJsonArray outgoing(QJsonArray message) const;
    QJsonArray incoming(QJsonArray message) const;
    QStringList arguments(const QString &program, const QStringList &args = {}) const;
    static QString environmentScript(const QString &provider = "auto");
    static QString windowsText(const QByteArray &bytes);
    static int distributionVersion(const QString &listing, const QString &distribution);
    static QString relativePath(const QString &directory, const QString &absolute);
};
#endif
