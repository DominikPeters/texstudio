#include "texpressowsl.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

QStringList TexpressoWsl::arguments(const QString &program, const QStringList &args) const
{
    QStringList result;
    if (!distribution.isEmpty()) result << "--distribution" << distribution;
    return result << "--exec" << program << args;
}
QString TexpressoWsl::environmentScript()
{
    // No login shell startup noise on stdout: it carries binary PNG frames.
    return QStringLiteral("for p in /usr/local/texlive/*/bin/* \"$HOME\"/.local/share/texlive/*/bin/*; do "
                          "[ ! -x \"$p/kpsewhich\" ] || PATH=\"$p:$PATH\"; done; export PATH; "
                          "if ! command -v kpsewhich >/dev/null 2>&1; then "
                          "printf '%s\\n' 'TeX Live was not found inside the selected WSL distribution. Use WSL Setup to install it or select another installation.' >&2; exit 126; fi; exec \"$@\"");
}
void TexpressoWsl::prepare(const QString &root)
{
    QString normalized = root; normalized.replace('\\', '/');
    const QRegularExpression unc("^//(?:wsl\\.localhost|wsl\\$)/([^/]+)/", QRegularExpression::CaseInsensitiveOption);
    const auto match = unc.match(normalized);
    if (distribution.isEmpty() && match.hasMatch()) distribution = match.captured(1);
    QProcess check;
    QStringList roots;
    for (const auto &drive : QDir::drives()) {
        const auto path = drive.absoluteFilePath();
        if (path.size() >= 2 && path[1] == ':') roots << path;
    }
    QStringList command = {"-c",
        "printf 'DIST\\t%s\\n' \"$WSL_DISTRO_NAME\"; for p do "
        "v=$(wslpath -u \"$p\" 2>/dev/null) || continue; printf 'DRIVE\\t%s\\t%s\\n' \"$p\" \"$v\"; done",
        "texstudio-wsl"};
    command << roots;
    check.start("wsl.exe", arguments("sh", command));
    if (!check.waitForStarted(10000) || !check.waitForFinished(30000)) {
        check.kill(); check.waitForFinished(1000);
        error = QStringLiteral("Cannot start WSL. Install WSL and a distribution, or use WSL Setup. ") + check.errorString(); return;
    }
    if (check.exitCode() != 0) {
        error = QStringLiteral("Cannot start the selected WSL distribution: ") + QString::fromUtf8(check.readAllStandardError()); return;
    }
    for (const auto &line : QString::fromUtf8(check.readAllStandardOutput()).split('\n')) {
        const auto parts = line.trimmed().split('\t');
        if (parts.size() == 2 && parts[0] == "DIST") distribution = parts[1];
        if (parts.size() == 3 && parts[0] == "DRIVE") {
            QString drive = parts[1]; drive.replace('\\', '/');
            QString mount = parts[2];
            if (!drive.endsWith('/')) drive += '/';
            if (!mount.endsWith('/')) mount += '/';
            drives.insert(drive.toUpper(), mount);
        }
    }
    if (distribution.isEmpty()) error = QStringLiteral("WSL did not identify its distribution. Check the WSL installation.");
}
QString TexpressoWsl::toEngine(QString path) const
{
    if (!enabled) return path;
    path.replace('\\', '/');
    const QRegularExpression unc("^//(?:wsl\\.localhost|wsl\\$)/([^/]+)(/.*)$", QRegularExpression::CaseInsensitiveOption);
    const auto match = unc.match(path);
    if (match.hasMatch() && match.captured(1).compare(distribution, Qt::CaseInsensitive) == 0) return match.captured(2);
    for (auto it = drives.cbegin(); it != drives.cend(); ++it)
        if (path.startsWith(it.key(), Qt::CaseInsensitive)) return it.value() + path.mid(it.key().size());
    return path;
}
QString TexpressoWsl::toEditor(QString path) const
{
    if (!enabled || !path.startsWith('/')) return path;
    for (auto it = drives.cbegin(); it != drives.cend(); ++it)
        if (path.startsWith(it.value())) return it.key() + path.mid(it.value().size());
    return "//wsl.localhost/" + distribution + path;
}
QJsonArray TexpressoWsl::outgoing(QJsonArray message) const
{
    if (!enabled || message.isEmpty()) return message;
    const auto verb = message[0].toString();
    int index = -1;
    if (verb == "open" || verb == "close" || verb == "change-range" || verb == "synctex-forward") index = 1;
    if (verb == "preview-search" || verb == "preview-prepare") index = 2;
    if (index >= 0 && message.size() > index && message[index].isString()) message[index] = toEngine(message[index].toString());
    return message;
}
QJsonArray TexpressoWsl::incoming(QJsonArray message) const
{
    if (!enabled || message.isEmpty()) return message;
    const auto verb = message[0].toString();
    int index = verb == "input-file" ? 2 : verb == "lookup-file" ? 3 : verb == "synctex" ? 1 : -1;
    if (index >= 0 && message.size() > index && message[index].isString()) message[index] = toEditor(message[index].toString());
    return message;
}
