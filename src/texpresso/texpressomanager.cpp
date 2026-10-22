#include "texpressomanager.h"
#include <QFileInfo>

TexpressoManager::TexpressoManager(QObject *parent, const QString &engine)
    : QObject(parent), engineFile(engine.isEmpty() ? TexpressoProcess::bundledEngine() : engine) {}
TexpressoManager::~TexpressoManager() { shutdown(); }
void TexpressoManager::activate(const QString &root)
{
    const auto key = root.isEmpty() ? QString() : TexpressoSession::identity(root);
    if (selected && TexpressoSession::identity(selected->root()) == key) return;
    if (selected) selected->setActive(false);
    selected = nullptr;
    if (!key.isEmpty() && QFileInfo(root).suffix().compare("tex", Qt::CaseInsensitive) == 0 && QFileInfo::exists(root)) {
        auto session = sessions.value(key);
        if (!session) {
            session = new TexpressoSession(root, engineFile, buffers, this);
            sessions[key] = session;
            connect(session, &TexpressoSession::updated, this, [this, session] { if (session == selected) emit updated(); });
            connect(session, &TexpressoSession::sourceLocation, this, [this, session](const QString &file, int line, int column, const QString &word) {
                if (session == selected) emit sourceLocation(file, line, column, word);
            });
            selected = session;
            const auto build = externalBuilds.value(key);
            for (int i = 0; i < build.depth; ++i) session->beginExternalBuild(build.auxiliaryDirectories);
            if (!build.depth && lastAuxiliaryDirectories.contains(key)) {
                session->beginExternalBuild(lastAuxiliaryDirectories.value(key)); session->endExternalBuild(true);
            }
            session->setActive(true); session->start();
        } else { selected = session; selected->setActive(true); }
    }
    emit updated();
}
void TexpressoManager::openBuffer(const QString &path, const QString &text)
{
    if (path.isEmpty()) return;
    const auto key = TexpressoSession::identity(path);
    if (buffers.contains(key) && buffers[key] == text) return;
    buffers[key] = text;
    for (auto session : sessions) session->openBuffer(path, text);
}
void TexpressoManager::reconcileBuffer(const QString &path, const QString &text) { openBuffer(path, text); }
void TexpressoManager::changeBuffer(const QString &path, int sl, int sc, int el, int ec, const QString &text, const QString &currentText)
{
    if (path.isEmpty()) return;
    const auto key = TexpressoSession::identity(path);
    if (!buffers.contains(key)) { openBuffer(path, currentText); return; }
    buffers[key] = currentText;
    for (auto session : sessions) session->changeBuffer(path, sl, sc, el, ec, text, currentText);
}
void TexpressoManager::closeBuffer(const QString &path)
{
    buffers.remove(TexpressoSession::identity(path));
    for (auto session : sessions) session->closeBuffer(path);
}
void TexpressoManager::retainProjects(const QSet<QString> &roots)
{
    for (auto it = sessions.begin(); it != sessions.end();) {
        if (!roots.contains(it.key())) {
            auto session = it.value(); it = sessions.erase(it);
            if (selected == session) selected = nullptr;
            delete session;
        } else ++it;
    }
    emit updated();
}
void TexpressoManager::restart()
{
    if (!selected) return;
    selected->restart();
}
void TexpressoManager::beginExternalBuild(const QString &root, const QStringList &auxiliaryDirectories)
{
    if (root.isEmpty()) return;
    const auto key = TexpressoSession::identity(root);
    auto &build = externalBuilds[key];
    if (!build.depth) { build.success = true; build.auxiliaryDirectories.clear(); }
    ++build.depth;
    for (const auto &directory : auxiliaryDirectories) {
        if (!build.auxiliaryDirectories.contains(directory)) build.auxiliaryDirectories.append(directory);
    }
    if (auto live = sessions.value(key)) live->beginExternalBuild(auxiliaryDirectories);
}
QStringList TexpressoManager::auxiliaryDirectories(const QString &root) const
{
    const auto key = TexpressoSession::identity(root);
    const auto build = externalBuilds.constFind(key);
    return build != externalBuilds.cend() && build->depth
        ? build->auxiliaryDirectories : lastAuxiliaryDirectories.value(key);
}
void TexpressoManager::endExternalBuild(const QString &root, bool success)
{
    const auto key = TexpressoSession::identity(root);
    auto found = externalBuilds.find(key);
    if (found == externalBuilds.end() || !found->depth) return;
    found->success &= success;
    --found->depth;
    if (auto live = sessions.value(key)) live->endExternalBuild(found->success);
    if (!found->depth) {
        if (found->success) lastAuxiliaryDirectories[key] = found->auxiliaryDirectories;
        externalBuilds.erase(found);
    }
}

void TexpressoManager::shutdown()
{
    selected = nullptr;
    qDeleteAll(sessions); sessions.clear(); externalBuilds.clear(); lastAuxiliaryDirectories.clear();
}
