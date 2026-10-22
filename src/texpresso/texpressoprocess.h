#ifndef TEXPRESSOPROCESS_H
#define TEXPRESSOPROCESS_H
#include "texpressoprotocol.h"
#include <QProcess>
#include <QTimer>

class TexpressoChild : public QProcess
{
public:
    explicit TexpressoChild(QObject *parent = nullptr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
protected:
    void setupChildProcess() override;
#endif
};

class TexpressoProcess : public QObject
{
    Q_OBJECT
public:
    explicit TexpressoProcess(QObject *parent = nullptr);
    ~TexpressoProcess() override;
    void start(const QString &engine, const QString &root, const QStringList &inclusionDirectories = {});
    void send(const QJsonArray &command);
    void stop();
    bool running() const { return child.state() != QProcess::NotRunning; }
    qint64 processId() const { return child.processId(); }
    static QString bundledEngine();
    static QProcessEnvironment texliveEnvironment();
signals:
    void ready(const QJsonObject &capabilities);
    void message(const QJsonArray &message);
    void image(const QJsonObject &header, const QByteArray &bytes);
    void diagnostic(const QString &text);
    void failed(const QString &reason);
    void stopped();
private:
    TexpressoChild child;
    QProcess probe;
    TexpressoProtocol protocol;
    QTimer probeTimeout, shutdownTimer;
    QString program, rootFile;
    QStringList inclusionDirectories;
    qint64 processGroup = 0;
    bool stopping = false;
    void killGroup();
};
#endif
