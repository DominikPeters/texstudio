#include "texpressosession.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <limits>

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
    : QObject(parent), preview(this), rootFile(identity(root)), engineFile(engine), buffers(initial),
      prewarm([this] { return preview.nextRequestId(); }, this)
{
    status = tr("Starting live preview…");
    syncFile = rootFile;
    aliases[identity(root)].insert(rootFile);
    rescanTimer.setSingleShot(true); rescanTimer.setInterval(200);
    compilerOutputTimer.setSingleShot(true);
    connect(&compilerOutputTimer, &QTimer::timeout, this, &TexpressoSession::refreshCompilerOutput);
    connect(&prewarm, &TexpressoPrewarm::command, this, [this](const QJsonArray &command) {
        if (process && ready) process->send(command);
    });
    connect(&prewarm, &TexpressoPrewarm::pendingChanged, &preview, &TexpressoPreview::setPreparationPending);
    connect(&prewarm, &TexpressoPrewarm::started, &preview, &TexpressoPreview::invalidateNativePage);
    connect(&preview, &TexpressoPreview::activityChanged, this, &TexpressoSession::refreshPrewarmContext);
    connect(&preview, &TexpressoPreview::interactionStarted, &prewarm, [this] { prewarm.interrupt(); });
    connect(&preview, &TexpressoPreview::manualNavigated, this, [this] { sourceFollow.suspend(); });
    connect(&preview, &TexpressoPreview::failed, this, [this](const QString &reason) { previewError = reason; emit updated(); });
    connect(&preview, &TexpressoPreview::updated, this, [this] {
        if (preview.valid()) previewError.clear();
        refreshPrewarmContext();
        const QString frame = QString::number(preview.image().cacheKey());
        if (preview.valid() && frame != lastPreparationFrame) {
            lastPreparationFrame = frame;
            prewarm.refresh(true);
        }
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
        supportsPreparation = features.value("prepareEdit").toBool();
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
        emit outputUpdated();
    });
    connect(process.get(), &TexpressoProcess::failed, this, &TexpressoSession::error);
    connect(&preview, &TexpressoPreview::command, process.get(), &TexpressoProcess::send);
}
TexpressoSession::~TexpressoSession()
{
    foreground = false; ready = false; prewarm.setContext({});
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
    sourceFollow.invalidate();
    ready = false; resumePending = false;
    preview.setActive(false);
    prewarm.setContext({}); prewarm.reset();
    supportsPreparation = false; lastPreparationFrame.clear();
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
    compilerOutputTimer.stop();
    outputLines.clear(); logLines.clear(); compilerOutput.clear(); engineOutput.clear();
    compilerLogSequence = -1; compilerLog.clear();
    previewError.clear(); compilerError.clear(); compilerWarning.clear();
    failure = false;
    emit outputUpdated();
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
    if (foreground && !userPaused && !userStopped && !externalBuildRunning()) {
        sourceFollow.invalidate();
        sendSource({"rerun-once"});
    }
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
    prewarm.interrupt();
    previewError.clear();
    process->send(command);
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
    refreshPrewarmContext();
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
    const auto key = identity(path);
    if (!buffers.contains(key) || buffers.value(key) != text) ++bufferVersions[key];
    buffers[key] = text;
    for (const auto &alias : aliases.value(key)) sendSource({"open", alias, text});
    if (key == preparationPath) prepareEdit(key, preparationLine, preparationColumn);
}
void TexpressoSession::replaceBuffer(const QString &path, const QString &text) { openBuffer(path, text); }
void TexpressoSession::changeBuffer(const QString &path, int sl, int sc, int el, int ec, const QString &text, const QString &currentText)
{
    suppressRestartSync = false;
    const auto key = identity(path); buffers[key] = currentText; ++bufferVersions[key];
    for (const auto &alias : aliases.value(key)) sendSource({"change-range", alias, sl, sc, el, ec, text});
    if (key == preparationPath) prepareEdit(key, preparationLine, preparationColumn);
}
void TexpressoSession::closeBuffer(const QString &path)
{
    suppressRestartSync = false;
    const auto key = identity(path); buffers.remove(key);
    ++bufferVersions[key];
    if (key == preparationPath) { preparationPath.clear(); prewarm.select(); }
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
    prewarm.message(m);
    const auto verb = m[0].toString();
    if (verb == "preview-state" && m.size() == 6) {
        acknowledgedSource = std::max(acknowledgedSource, qint64(m[2].toDouble()));
        if (resumePending && acknowledgedSource == preview.sequence() && foreground && !userPaused && !userStopped && !externalBuildRunning()) {
            resumePending = false; process->send({"resume"});
            syncForward(syncFile, syncLine, false, syncColumn);
        }
    } else if (verb == "input-file" && m.size() == 3) {
        const auto logical = absolute(m[2].toString());
        const auto editor = editorPaths.value(logical, identity(logical));
        announce(logical, editor); inputAliases[editor] = logical;
        prewarm.refresh(true);
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
        if (!compilerOutputTimer.isActive()) compilerOutputTimer.start();
    } else if (verb == "truncate-lines" && m.size() == 3) {
        auto &lines = m[1] == "out" ? outputLines : logLines;
        const int keep = std::clamp(m[2].toInt(), 0, int(lines.size()));
        while (lines.size() > keep) lines.removeLast();
        if (!compilerOutputTimer.isActive()) compilerOutputTimer.start();
    } else if (verb == "flush") {
        // A protocol flush makes diagnostics immediately observable. Without
        // one, publish once when this batch of incoming messages completes.
        if (compilerOutputTimer.isActive()) refreshCompilerOutput();
        emit updated();
        // Match the extension's automatic source synchronization after a pass.
        if (acknowledgedSource == preview.sequence() && foreground && !userPaused && !externalBuildRunning())
            syncForward(syncFile, syncLine, false, syncColumn);
    }
}
void TexpressoSession::refreshCompilerOutput()
{
    compilerOutputTimer.stop();
    compilerLogSequence = acknowledgedSource == preview.sequence() ? preview.sequence() : -1;
    compilerLog = (logLines.isEmpty() ? outputLines : logLines).join('\n');
    compilerOutput = outputLines.join('\n') + '\n' + logLines.join('\n');
    const auto oldError = compilerError, oldWarning = compilerWarning;
    compilerError.clear(); compilerWarning.clear();
    // Prefer the original error over TeX's subsequent "Emergency stop".
    // Both streams are checkpointed: recompute after truncation as well as
    // append, so a repaired document cannot retain a stale diagnostic.
    for (const auto &lines : {outputLines, logLines}) {
        for (int i = 0; i < lines.size(); ++i) {
            const auto line = lines[i].trimmed();
            if (!line.startsWith("! ")) continue;
            compilerError = line.mid(2).trimmed();
            for (int j = i + 1; j < std::min(i + 12, int(lines.size())); ++j) {
                const auto context = lines[j].trimmed();
                if (context.startsWith("! ")) break;
                if (context.startsWith("l.") && context.size() > 2 && context[2].isDigit()) {
                    compilerError += '\n' + context;
                    break;
                }
            }
            break;
        }
        if (!compilerError.isEmpty()) break;
    }
    static const QRegularExpression bibliographyTool("Please\\s+\\(re\\)run\\s+(Biber|BibTeX)", QRegularExpression::CaseInsensitiveOption);
    const auto tool = bibliographyTool.match(compilerOutput);
    if (tool.hasMatch()) {
        compilerWarning = tr("Bibliography needs %1. Use Build & Refresh to update it.").arg(tool.captured(1));
    } else {
        static const QRegularExpression missingBibliography("(?:^|\\n)\\s*No file [^\\n]+\\.bbl\\.", QRegularExpression::CaseInsensitiveOption);
        if (missingBibliography.match(compilerOutput).hasMatch())
            compilerWarning = tr("Bibliography output is missing. Use Build & Refresh to run BibTeX or Biber.");
    }
    emit outputUpdated();
    if (compilerError != oldError || compilerWarning != oldWarning) emit updated();
}
void TexpressoSession::refreshPrewarmContext()
{
    prewarm.setContext({ready && supportsPreparation && foreground && !userPaused && !userStopped
        && !failure && !externalBuildRunning(), preview.valid() && !preview.busy() && !preview.followingSource(),
        preview.revision(), preview.sequence()});
}
void TexpressoSession::prepareEdit(const QString &path, int line, int column)
{
    preparationPath = identity(path); preparationLine = line; preparationColumn = column;
    const auto key = preparationPath;
    const auto version = bufferVersions.value(key);
    const QString selection = key + ':' + QString::number(version) + ':' + QString::number(line) + ':' + QString::number(column);
    prewarm.select(selection, [this, key, version, line, column]() -> std::optional<TexpressoPrewarm::Position> {
        if (!buffers.contains(key) || bufferVersions.value(key) != version || line < 0 || column < 0) return {};
        const auto known = aliases.value(key);
        if (known.isEmpty()) return {};
        const auto &text = buffers[key];
        qsizetype offset = 0;
        for (int i = 0; i < line; ++i) {
            offset = text.indexOf('\n', offset);
            if (offset < 0) return {};
            ++offset;
        }
        auto end = text.indexOf('\n', offset);
        if (end < 0) end = text.size();
        offset += qMin(qsizetype(column), end - offset);
        // A cursor cannot split a Unicode scalar; keep synthetic/test positions
        // consistent with the editor before converting UTF-16 to engine bytes.
        if (offset > 0 && offset < text.size() && text[offset].isLowSurrogate() && text[offset - 1].isHighSurrogate()) --offset;
        const auto bytes = text.left(offset).toUtf8().size();
        if (bytes > std::numeric_limits<int>::max()) return {};
        return TexpressoPrewarm::Position{inputAliases.value(key, *known.cbegin()), int(bytes)};
    });
    refreshPrewarmContext();
}
void TexpressoSession::syncForward(const QString &path, int line, bool explicitRequest, int column)
{
    const auto key = identity(path);
    syncFile = key; syncLine = line; syncColumn = column;
    if (explicitRequest) suppressRestartSync = false;
    if (!ready || !foreground || userPaused || userStopped || failure || externalBuildRunning()
        || (!explicitRequest && (!followCursor || suppressRestartSync))) return;
    const auto known = aliases.value(key);
    if (known.isEmpty()) return;
    if (!sourceFollow.request({key, bufferVersions.value(key), line, column,
        preview.sequence(), preview.revision()}, explicitRequest)) return;
    preview.forward(inputAliases.value(key, *known.cbegin()), line);
}
