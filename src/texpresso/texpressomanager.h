#ifndef TEXPRESSOMANAGER_H
#define TEXPRESSOMANAGER_H
#include "texpressosession.h"

// Project identity is independent of the selected editor tab and preview widget.
class TexpressoManager : public QObject
{
    Q_OBJECT
public:
    explicit TexpressoManager(QObject *parent = nullptr, const QString &engine = QString());
    ~TexpressoManager() override;
    void activate(const QString &root);
    void openBuffer(const QString &path, const QString &text);
    void reconcileBuffer(const QString &path, const QString &text);
    void changeBuffer(const QString &path, int sl, int sc, int el, int ec, const QString &text, const QString &currentText);
    void closeBuffer(const QString &path);
    void retainProjects(const QSet<QString> &roots);
    void restart();
    TexpressoSession *current() const { return selected; }
    TexpressoSession *session(const QString &root) const { return sessions.value(TexpressoSession::identity(root)); }
    int sessionCount() const { return sessions.size(); }
    void shutdown();
signals:
    void updated();
    void sourceLocation(const QString &file, int oneBasedLine, int column, const QString &word);
private:
    QString engineFile;
    TexpressoSession::Buffers buffers;
    QMap<QString, TexpressoSession *> sessions;
    TexpressoSession *selected = nullptr;
};
#endif
