#include "texpressosession.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>

QString TexpressoSession::identity(const QString &path)
{
    const QFileInfo file(path);
    const auto canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? file.absoluteFilePath() : canonical;
}
QString TexpressoSession::absolute(const QString &path) const
{
    return QDir::cleanPath(QFileInfo(path).isAbsolute() ? path : QDir(QFileInfo(rootFile).absolutePath()).absoluteFilePath(path));
}
TexpressoSession::TexpressoSession(const QString &root, const QString &engine, const Buffers &initial, QObject *parent)
    : QObject(parent), preview(this), rootFile(identity(root)), engineFile(engine), buffers(initial)
{
    status = tr("Starting live preview…");
    syncFile = rootFile;
    aliases[identity(root)].insert(rootFile);
    rescanTimer.setSingleShot(true); rescanTimer.setInterval(200);
    connect(&preview, &TexpressoPreview::failed, this, [this](const QString &reason) { previewError = reason; emit updated(); });
    connect(&preview, &TexpressoPreview::updated, this, [this] {
        if (preview.valid()) previewError.clear();
        emit updated();
    });
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (externalBuildRunning()) return;
        pendingDependencies.append(path); rescanTimer.start();
    });
    connect(&rescanTimer, &QTimer::timeout, this, [this] {
        if (externalBuildRunning()) { pendingDependencies.clear(); return; }
        bool changed = false;
        for (const auto &path : pendingDependencies) {
            if (!buffers.contains(identity(path)) && !writtenFiles.contains(identity(path))) changed = true;
            if (QFileInfo::exists(path) && !watcher.files().contains(path)) watcher.addPath(path);
        }
        pendingDependencies.clear();
        if (changed) sendSource({"rescan"});
    });
}
void TexpressoSession::createProcess()
{
    process = std::make_unique<TexpressoProcess>();
    connect(process.get(), &TexpressoProcess::ready, this, [this](const QJsonObject &features) {
        preview.setCapabilities(features);
        ready = true;
        // Pause stepping while priming the editor overlays. Input remains live.
        process->send({"pause"});
        // Like the extension, attach included buffers when the engine announces
        // them; eagerly opening unrelated files causes unnecessary invalidation.
        if (buffers.contains(identity(rootFile))) sendSource({"open", rootFile, buffers.value(identity(rootFile))});
        process->send({"rerun", automaticReruns});
        // A new child starts with sequence zero; acknowledge the retained
        // preview generation even when the root has no editor overlay.
        process->send({"preview-barrier", double(preview.sequence())});
        applyPause(); emit updated();
    });
    connect(process.get(), &TexpressoProcess::message, this, &TexpressoSession::message);
    connect(process.get(), &TexpressoProcess::image, &preview, &TexpressoPreview::receive);
    connect(process.get(), &TexpressoProcess::diagnostic, this, [this](const QString &text) {
        engineOutput = (engineOutput + text).right(64000);
    });
    connect(process.get(), &TexpressoProcess::failed, this, &TexpressoSession::error);
    connect(&preview, &TexpressoPreview::command, process.get(), &TexpressoProcess::send);
}
TexpressoSession::~TexpressoSession()
{
    if (process) {
        disconnect(process.get(), nullptr, this, nullptr);
        disconnect(process.get(), nullptr, &preview, nullptr);
        process.reset(); // Reap the entire process group before destroying members.
    }
}
void TexpressoSession::start()
{
    if (process || userStopped || externalBuildRunning()) return;
    createProcess(); process->start(engineFile, rootFile, auxiliaryDirectories);
}
void TexpressoSession::resetEngine()
{
    ready = false; resumePending = false;
    preview.setActive(false);
    preview.sourceChanged(preview.sequence() + 1);
    if (process) {
        disconnect(process.get(), nullptr, this, nullptr);
        disconnect(process.get(), nullptr, &preview, nullptr);
        process.reset();
    }
    if (!watcher.files().isEmpty()) watcher.removePaths(watcher.files());
    rescanTimer.stop(); pendingDependencies.clear(); writtenFiles.clear();
    aliases.clear(); aliases[identity(rootFile)].insert(rootFile);
    editorPaths.clear(); inputAliases.clear(); acknowledgedSource = 0;
    outputLines.clear(); logLines.clear(); compilerOutput.clear(); engineOutput.clear();
    previewError.clear(); failure = false; syncAfterCompile = false;
    suppressRestartSync = true;
}
void TexpressoSession::restart()
{
    userStopped = false;
    resetEngine(); start(); applyPause();
}
void TexpressoSession::setAutoRerun(bool enabled)
{
    automaticReruns = enabled;
    if (ready && process) process->send({"rerun", enabled});
    emit updated();
}
void TexpressoSession::rerunOnce()
{
    if (foreground && !userPaused && !userStopped && !externalBuildRunning()) sendSource({"rerun-once"});
}
void TexpressoSession::beginExternalBuild(const QStringList &directories)
{
    if (externalBuildDepth++ == 0) {
        previousAuxiliaryDirectories = auxiliaryDirectories;
        auxiliaryDirectories.clear();
        externalBuildSucceeded = true;
        pendingDependencies.clear(); rescanTimer.stop();
        applyPause();
    }
    if (!directories.isEmpty()) {
        for (const auto &directory : directories) {
            const auto path = absolute(directory);
            if (!auxiliaryDirectories.contains(path)) auxiliaryDirectories.append(path);
        }
    }
}
void TexpressoSession::endExternalBuild(bool success)
{
    if (!externalBuildDepth) return;
    externalBuildSucceeded &= success;
    if (--externalBuildDepth) return;
    if (!externalBuildSucceeded) auxiliaryDirectories = previousAuxiliaryDirectories;
    if (externalBuildSucceeded && !userStopped) {
        resetEngine(); start();
    } else if (!process && !userStopped) start();
    // A failed build keeps the existing process and overlays. The engine may
    // still observe disk changes through its own periodic filesystem scan.
    applyPause();
}
void TexpressoSession::error(const QString &reason)
{
    if (userStopped || failure) return;
    failure = true; ready = false; status = reason;
    preview.setActive(false); if (process) process->stop(); emit updated();
}
void TexpressoSession::sendSource(QJsonArray command)
{
    if (!ready || failure || userStopped) return;
    previewError.clear();
    process->send(command);
    syncAfterCompile = !suppressRestartSync;
    const auto sequence = preview.sequence() + 1;
    preview.sourceChanged(sequence);
    process->send({"preview-barrier", double(sequence)});
}
void TexpressoSession::setActive(bool active)
{
    foreground = active; applyPause();
    if (!active) preview.releaseCache(); // Retain its displayed frame, not extra pages.
}
void TexpressoSession::setPaused(bool paused) { userPaused = paused; applyPause(); }
void TexpressoSession::applyPause()
{
    const bool run = foreground && !userPaused && !userStopped && !failure && !externalBuildRunning();
    preview.setActive(run);
    if (ready) {
        // Let the engine commit edits received while paused before stepping it.
        // A resume in the same stdin transaction can advance the old snapshot.
        resumePending = run && acknowledgedSource < preview.sequence();
        if (!run || !resumePending) {
            process->send({run ? "resume" : "pause"});
        }
    }
    if (!failure && !userStopped) status = externalBuildRunning() ? tr("External build running…") : !foreground ? tr("Project inactive") : userPaused ? tr("Compilation paused") : tr("Live preview");
    emit updated();
}
void TexpressoSession::stop()
{
    userStopped = true; ready = false; status = tr("Live preview stopped");
    preview.setActive(false); if (process) process->stop(); emit updated();
}
void TexpressoSession::openBuffer(const QString &path, const QString &text)
{
    suppressRestartSync = false;
    const auto key = identity(path); buffers[key] = text;
    for (const auto &alias : aliases.value(key)) sendSource({"open", alias, text});
}
void TexpressoSession::replaceBuffer(const QString &path, const QString &text) { openBuffer(path, text); }
void TexpressoSession::changeBuffer(const QString &path, int sl, int sc, int el, int ec, const QString &text, const QString &currentText)
{
    suppressRestartSync = false;
    const auto key = identity(path); buffers[key] = currentText;
    if (aliases.contains(key)) { syncFile = path; syncLine = sl; }
    for (const auto &alias : aliases.value(key)) sendSource({"change-range", alias, sl, sc, el, ec, text});
}
void TexpressoSession::closeBuffer(const QString &path)
{
    suppressRestartSync = false;
    const auto key = identity(path); buffers.remove(key);
    for (const auto &alias : aliases.value(key)) sendSource({"close", alias});
}
void TexpressoSession::announce(const QString &path, const QString &editorPath)
{
    const auto logical = absolute(path), key = identity(editorPath.isEmpty() ? logical : editorPath);
    editorPaths[logical] = key;
    if (!aliases[key].contains(logical)) {
        aliases[key].insert(logical);
        if (buffers.contains(key)) sendSource({"open", logical, buffers.value(key)});
    }
    if (!writtenFiles.contains(key) && QFileInfo::exists(logical) && !watcher.files().contains(logical)) watcher.addPath(logical);
}
void TexpressoSession::message(const QJsonArray &m)
{
    preview.message(m);
    const auto verb = m[0].toString();
    if (verb == "preview-state" && m.size() == 6) {
        acknowledgedSource = std::max(acknowledgedSource, qint64(m[2].toDouble()));
        if (resumePending && acknowledgedSource == preview.sequence() && foreground && !userPaused && !userStopped && !externalBuildRunning()) {
            resumePending = false; process->send({"resume"});
            if (followCursor && !suppressRestartSync) syncForward(syncFile, syncLine);
        }
    } else if (verb == "input-file" && m.size() == 3) {
        const auto logical = absolute(m[2].toString());
        const auto editor = editorPaths.value(logical, identity(logical));
        announce(logical, editor); inputAliases[editor] = logical;
    }
    else if (verb == "lookup-file" && m.size() == 4 && m[1] == "write") {
        const auto path = absolute(m[3].toString());
        writtenFiles.insert(identity(path)); watcher.removePath(path);
    } else if (verb == "lookup-file" && m.size() == 4 && m[1] == "read" && m[2] == "failed") {
        const auto path = absolute(m[3].toString());
        // XeTeX may probe an extensionless name before falling back to TeX
        // Live's disk lookup. Provide the editor overlay under that VFS alias
        // too, so \\input{chapter} uses unsaved chapter.tex contents.
        if (QFileInfo(path).suffix().isEmpty() && !QFileInfo::exists(path)) {
            const auto candidate = identity(path + ".tex");
            if (buffers.contains(candidate)) announce(path, candidate);
        }
    } else if (verb == "lookup-file" && m.size() == 4 && m[1] == "read" && m[2] == "successful") {
        const auto path = absolute(m[3].toString());
        // Watch project resources, not the installed TeX distribution.
        if (path.startsWith(QFileInfo(rootFile).absolutePath() + '/')) announce(path);
    } else if (verb == "reset-sync") {
        for (auto it = aliases.cbegin(); it != aliases.cend(); ++it)
            if (buffers.contains(it.key())) for (const auto &alias : it.value()) sendSource({"open", alias, buffers.value(it.key())});
    } else if (verb == "synctex" && m.size() >= 3 && preview.valid()) {
        const auto logical = absolute(m[1].toString());
        emit sourceLocation(editorPaths.value(logical, logical), m[2].toInt(), m.size() > 3 ? m[3].toInt(-1) : -1, m.size() > 4 ? m[4].toString() : QString());
    } else if (verb == "append-lines" && m.size() >= 2) {
        auto &lines = m[1] == "out" ? outputLines : logLines;
        for (int i = 2; i < m.size(); ++i) lines.append(m[i].toString());
    } else if (verb == "truncate-lines" && m.size() == 3) {
        auto &lines = m[1] == "out" ? outputLines : logLines;
        const int keep = std::clamp(m[2].toInt(), 0, int(lines.size()));
        while (lines.size() > keep) lines.removeLast();
    } else if (verb == "flush") {
        compilerOutput = outputLines.join('\n') + '\n' + logLines.join('\n'); emit updated();
        // Match the extension's automatic source synchronization after a pass.
        if (syncAfterCompile && acknowledgedSource == preview.sequence() && foreground && !userPaused && !externalBuildRunning()) {
            syncAfterCompile = false;
            if (followCursor && !suppressRestartSync) syncForward(syncFile, syncLine);
        }
    }
}
void TexpressoSession::syncForward(const QString &path, int line)
{
    const auto known = aliases.value(identity(path));
    if (!known.isEmpty()) {
        syncFile = path; syncLine = line;
        preview.forward(inputAliases.value(identity(path), *known.cbegin()), line);
    }
}
