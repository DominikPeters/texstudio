#ifndef TEXPRESSOPREWARM_H
#define TEXPRESSOPREWARM_H
#include <QObject>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

// Mirrors the extension's EditPrewarm scheduler. Only speculative work waits
// for the cursor; edits and foreground image requests never wait for it.
class TexpressoPrewarm : public QObject
{
    Q_OBJECT
public:
    struct Context { bool available = false, idle = false; qint64 revision = -1, sequence = 0; };
    struct Position { QString path; int byteOffset = 0; };
    using Resolver = std::function<std::optional<Position>()>;
    explicit TexpressoPrewarm(std::function<qint64()> nextId, QObject *parent = nullptr);
    void setContext(Context context);
    void select(const QString &identity = {}, Resolver resolve = {});
    void refresh(bool foregroundReady = false);
    void message(const QJsonArray &message);
    void interrupt(bool retry = true);
    void reset();
    bool pending() const { return cursorTimer.isActive() || flight.has_value(); }
signals:
    void command(const QJsonArray &command);
    void pendingChanged(bool pending);
    void started();
    void settled();
private:
    struct Target { QString identity; Resolver resolve; bool resolved = false; std::optional<Position> position; };
    struct Flight { qint64 id, revision, sequence; QString key; };
    void prepare();
    void remember(const QString &key);
    Context context;
    std::function<qint64()> nextId;
    std::optional<Target> target;
    std::optional<Flight> flight;
    QTimer cursorTimer, watchdog;
    QStringList attempts;
    QSet<QString> skipped;
};
#endif
