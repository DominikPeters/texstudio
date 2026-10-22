#ifndef TEXPRESSOPROTOCOL_H
#define TEXPRESSOPROTOCOL_H

#include <QObject>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>

// Protocol 1: JSON lines interleaved with exactly byteLength binary bytes.
// Ported from texpresso-vscode/src/preview-protocol.ts; stdout is never text-decoded.
class TexpressoProtocol : public QObject
{
    Q_OBJECT
public:
    explicit TexpressoProtocol(QObject *parent = nullptr) : QObject(parent) {}
    void feed(const QByteArray &bytes);
    void finish();
    void reset();
    static bool validImage(const QJsonObject &header);
signals:
    void message(const QJsonArray &message);
    void image(const QJsonObject &header, const QByteArray &bytes);
    void failed(const QString &reason);
private:
    void fail(const QString &reason);
    QByteArray pending;
    QJsonObject header;
    qint64 bodyLength = -1;
    bool broken = false;
};
#endif
