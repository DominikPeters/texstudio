#ifndef TEXPRESSOSOURCEFOLLOW_H
#define TEXPRESSOSOURCEFOLLOW_H
#include <QString>
#include <optional>

// Ported from texpresso-vscode/src/source-follow.ts at d175622.
// Upstream MIT license: tests/sustained/LICENSE.upstream.
// One instance per session: compiler flushes acknowledge a selection, rather
// than creating another source-navigation intent.
class TexpressoSourceFollow
{
public:
    struct Intent {
        QString document;
        quint64 version = 0;
        int line = 0, column = 0;
        qint64 sequence = 0, revision = 0;
    };
    bool request(const Intent &intent, bool explicitRequest = false)
    {
        bool resumed = false;
        if (!explicitRequest && suspended) {
            if (!held) {
                held = intent;
                return false;
            }
            if (held->document == intent.document && held->version == intent.version
                && held->line == intent.line && held->column == intent.column) return false;
            resumed = true;
        }
        suspended = false;
        held.reset();
        if (!explicitRequest && !resumed && previous
            && previous->document == intent.document && previous->version == intent.version
            && previous->line == intent.line && previous->sequence == intent.sequence
            && previous->revision == intent.revision) {
            // SyncTeX searches by line; remember horizontal caret movement so
            // a later manual preview hold can distinguish a new selection.
            previous->column = intent.column;
            return false;
        }
        // Record before sending, including when sending synchronously flushes.
        previous = intent;
        return true;
    }
    void suspend() { suspended = true; held = previous; }
    void invalidate() { previous.reset(); } // Recovery preserves a manual hold.
private:
    std::optional<Intent> previous, held;
    bool suspended = false;
};
#endif
