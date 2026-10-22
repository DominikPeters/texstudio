#ifndef TEXPRESSOSESSION_H
#define TEXPRESSOSESSION_H
#include "texpressoprocess.h"
#include "texpressopreview.h"
#include <QMap>
#include <QSet>
#include <QFileSystemWatcher>
#include <QTimer>

class TexpressoSession : public QObject
{
    Q_OBJECT
public:
    using Buffers = QMap<QString, QString>;
    explicit TexpressoSession(const QString &root, const QString &engine, const Buffers &buffers, QObject *parent = nullptr);
    ~TexpressoSession() override;
    static QString identity(const QString &path);
    QString root() const { return rootFile; }
    void start();
    void setActive(bool active);
    void setPaused(bool paused);
    void stop();
    void openBuffer(const QString &path, const QString &text);
    void replaceBuffer(const QString &path, const QString &text);
    void changeBuffer(const QString &path, int startLine, int startColumn, int endLine, int endColumn, const QString &text, const QString &currentText);
    void closeBuffer(const QString &path);
    void syncForward(const QString &path, int line);
    TexpressoPreview preview;
    QString status;
    QString previewError;
    QString compilerOutput, engineOutput;
    bool userPaused = false;
    bool userStopped = false;
    bool followCursor = true;
    bool active() const { return foreground; }
    bool failed() const { return failure; }
    qint64 processId() const { return process.processId(); }
signals:
    void updated();
    void sourceLocation(const QString &file, int oneBasedLine, int column, const QString &word);
private:
    void sendSource(QJsonArray command);
    void message(const QJsonArray &message);
    void announce(const QString &path, const QString &editorPath = QString());
    QString absolute(const QString &path) const;
    void applyPause();
    QString rootFile, engineFile;
    Buffers buffers;
    QMap<QString, QSet<QString>> aliases;
    QMap<QString, QString> editorPaths, inputAliases;
    TexpressoProcess process;
    QFileSystemWatcher watcher;
    QTimer rescanTimer;
    QStringList pendingDependencies;
    QSet<QString> writtenFiles;
    QStringList outputLines, logLines;
    bool ready = false, foreground = false, failure = false;
    qint64 acknowledgedSource = 0;
    bool resumePending = false;
    bool syncAfterCompile = false;
    QString syncFile;
    int syncLine = 0;
    void error(const QString &reason);
};
#endif
