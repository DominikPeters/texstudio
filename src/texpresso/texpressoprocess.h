#ifndef TEXPRESSOPROCESS_H
#define TEXPRESSOPROCESS_H
#include "texpressoprotocol.h"
#include <QProcess>
#include <QTimer>
#include <QFutureWatcher>
#include "texpressowsl.h"

class TexpressoChild : public QProcess
{
public:
    explicit TexpressoChild(QObject *parent = nullptr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0) && !defined(Q_OS_WIN)
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
    void start(const QString &engine, const QString &root, const QStringList &inclusionDirectories = {}, int pixelLimit = TexpressoProtocol::maxPixels);
    void send(const QJsonArray &command);
    void stop();
    bool running() const { return child.state() != QProcess::NotRunning; }
    qint64 processId() const { return child.processId(); }
    static QString bundledEngine();
    static void setWslDistribution(const QString &distribution);
    static QProcessEnvironment texliveEnvironment();
    static void setPackageProvider(const QString &provider, bool installPackages = false);
signals:
    void ready(const QJsonObject &capabilities);
    void message(const QJsonArray &message);
    void image(const QJsonObject &header, const QByteArray &bytes);
    void diagnostic(const QString &text);
    void failed(const QString &reason);
    void stopped();
private:
    TexpressoChild child;
    TexpressoWsl wsl;
    QFutureWatcher<TexpressoWsl> initialization;
    void startProbe();
    QProcess probe;
    TexpressoProtocol protocol;
    QTimer probeTimeout, shutdownTimer;
    QString program, rootFile, provider;
    bool installPackages = false;
    QStringList inclusionDirectories;
    qint64 processGroup = 0;
    bool stopping = false, traceProtocol = false;
    int previewPixelLimit = TexpressoProtocol::maxPixels;
    void killGroup();
};
#endif
