#ifndef TEXPRESSOSESSION_H
#define TEXPRESSOSESSION_H
#include "texpressoprocess.h"
#include "texpressopreview.h"
#include <QMap>
#include <QSet>
#include <QFileSystemWatcher>
#include <QTimer>
#include <memory>

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
    void restart();
    void setAutoRerun(bool enabled);
    bool autoRerun() const { return automaticReruns; }
    void rerunOnce();
    // Balanced per recipe; nested builds share one paused session and restart.
    // Top-level directories replace the prior search path, including empty.
    void beginExternalBuild(const QStringList &auxiliaryDirectories = {});
    void endExternalBuild(bool success);
    bool externalBuildRunning() const { return externalBuildDepth > 0; }
    void openBuffer(const QString &path, const QString &text);
    void replaceBuffer(const QString &path, const QString &text);
    void changeBuffer(const QString &path, int startLine, int startColumn, int endLine, int endColumn, const QString &text, const QString &currentText);
    void closeBuffer(const QString &path);
    void syncForward(const QString &path, int line);
    TexpressoPreview preview;
    QString status;
    QString previewError;
    // Recoverable compiler diagnostics; a TeX error does not kill the live
    // process, since the next editor change can repair the document.
    QString compilerError, compilerWarning;
    QString compilerOutput, engineOutput;
    bool userPaused = false;
    bool userStopped = false;
    bool followCursor = true;
    bool active() const { return foreground; }
    bool failed() const { return failure; }
    qint64 processId() const { return process ? process->processId() : 0; }
signals:
    void updated();
    void outputUpdated();
    void sourceLocation(const QString &file, int oneBasedLine, int column, const QString &word);
private:
    friend class TexpressoDiagnosticsTest;
    void refreshCompilerOutput();
    void createProcess();
    void resetEngine();
    void sendSource(QJsonArray command);
    void message(const QJsonArray &message);
    void announce(const QString &path, const QString &editorPath = QString());
    QString absolute(const QString &path) const;
    void applyPause();
    QString rootFile, engineFile;
    Buffers buffers;
    QMap<QString, QSet<QString>> aliases;
    QMap<QString, QString> editorPaths, inputAliases;
    std::unique_ptr<TexpressoProcess> process;
    QStringList auxiliaryDirectories, previousAuxiliaryDirectories;
    int externalBuildDepth = 0;
    bool externalBuildSucceeded = true;
    bool automaticReruns = true;
    bool suppressRestartSync = false; // Retain view until the next editor mutation.
    QFileSystemWatcher watcher;
    QTimer rescanTimer;
    QTimer compilerOutputTimer;
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
