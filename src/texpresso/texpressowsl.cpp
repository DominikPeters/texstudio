#include "texpressowsl.h"
#include "texpressoprovider.h"
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
QString TexpressoWsl::environmentScript(const QString &provider)
{
    // No startup noise on stdout: it carries binary PNG frames.
    return TexpressoProvider::shellSetup(provider) + QStringLiteral(
        "if [ -z \"$TXP_PACKAGE_PROVIDER\" ]; then "
        "printf '%s\\n' 'TeX package provider was not found inside the selected WSL distribution. Use TeX Environment to select TeX Live or MiKTeX, or install TeX Live.' >&2; exit 126; fi; exec \"$@\"");
}
void TexpressoWsl::prepare(const QString &root)
{
    QString normalized = root; normalized.replace('\\', '/');
    const QRegularExpression unc("^//(?:wsl\\.localhost|wsl\\$)/([^/]+)/", QRegularExpression::CaseInsensitiveOption);
    const auto match = unc.match(normalized);
    if (distribution.isEmpty() && match.hasMatch()) distribution = match.captured(1);
    QProcess check;
#ifdef Q_OS_WIN
    check.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    QStringList roots;
    for (const auto &drive : QDir::drives()) {
        const auto path = drive.absoluteFilePath();
        if (path.size() >= 2 && path[1] == ':') roots << path;
    }
    QStringList command = {"-c",
        "printf 'DIST\\t%s\\nHOME\\t%s\\n' \"$WSL_DISTRO_NAME\" \"$HOME\"; "
        "df -Pk \"$HOME\" | awk 'END {printf \"FREE\\t%.0f\\n\", $4 * 1024}'; for p do "
        "v=$(wslpath -u \"$p\" 2>/dev/null) || continue; printf 'DRIVE\\t%s\\t%s\\n' \"$p\" \"$v\"; done",
        "texstudio-wsl"};
    command[1] += "; " + TexpressoProvider::shellSetup(requestedProvider) + "printf 'PROVIDER\\t%s\\n' \"$TXP_PACKAGE_PROVIDER\"";
    command << roots;
    check.start("wsl.exe", arguments("sh", command));
    if (!check.waitForStarted(10000) || !check.waitForFinished(30000)) {
        check.kill(); check.waitForFinished(1000);
        error = QStringLiteral("Cannot start WSL. Install WSL and a distribution, or use WSL Setup. ") + check.errorString(); return;
    }
    if (check.exitCode() != 0) {
        error = QStringLiteral("Cannot start the selected WSL distribution: ") + windowsText(check.readAllStandardError()); return;
    }
    for (const auto &line : QString::fromUtf8(check.readAllStandardOutput()).split('\n')) {
        const auto parts = line.trimmed().split('\t');
        if (parts.size() == 2 && parts[0] == "PROVIDER") provider = parts[1];
        if (parts.size() == 2 && parts[0] == "DIST") distribution = parts[1];
        if (parts.size() == 2 && parts[0] == "HOME") home = parts[1];
        if (parts.size() == 2 && parts[0] == "FREE") freeBytes = qint64(parts[1].toDouble());
        if (parts.size() == 3 && parts[0] == "DRIVE") {
            QString drive = parts[1]; drive.replace('\\', '/');
            QString mount = parts[2];
            if (!drive.endsWith('/')) drive += '/';
            if (!mount.endsWith('/')) mount += '/';
            drives.insert(drive.toUpper(), mount);
        }
    }
    if (distribution.isEmpty()) { error = QStringLiteral("WSL did not identify its distribution. Check the WSL installation."); return; }
    check.start("wsl.exe", {"--list", "--verbose"});
    if (!check.waitForStarted(10000) || !check.waitForFinished(10000)) {
        check.kill(); check.waitForFinished(1000);
        error = QStringLiteral("Could not check the selected distribution's WSL version. ") + check.errorString(); return;
    }
    const int version = distributionVersion(windowsText(check.readAllStandardOutput()), distribution);
    if (check.exitCode() != 0 || version < 0) error = QStringLiteral("Could not identify the selected distribution in wsl --list --verbose. ") + windowsText(check.readAllStandardError());
    else if (version != 2) error = QStringLiteral("Live preview requires WSL2. The selected distribution currently uses WSL1; convert it with wsl --set-version before starting the preview.");
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

QString TexpressoWsl::relativePath(const QString &directory, const QString &absolute)
{
    // QDir uses Windows case rules on Windows, but these are Linux namespaces.
    const auto from = QDir::cleanPath(directory).split('/', Qt::SkipEmptyParts);
    const auto to = QDir::cleanPath(absolute).split('/', Qt::SkipEmptyParts);
    int common = 0;
    while (common < from.size() && common < to.size() && from[common] == to[common]) ++common;
    QStringList parts;
    for (int i = common; i < from.size(); ++i) parts << "..";
    parts << to.mid(common);
    return parts.isEmpty() ? QStringLiteral(".") : parts.join('/');
}

QString TexpressoWsl::commandArgument(const QString &argument, const QString &workingDirectory) const
{
    QString prefix, path = argument;
    const int equals = argument.indexOf('=');
    if (equals >= 0) { prefix = argument.left(equals + 1); path = argument.mid(equals + 1); }
    QString normalized = path; normalized.replace('\\', '/');
    if (!(normalized.size() > 2 && normalized[1] == ':' && normalized[2] == '/')
        && !normalized.startsWith("//wsl.localhost/", Qt::CaseInsensitive)
        && !normalized.startsWith("//wsl$/", Qt::CaseInsensitive)) return argument;
    const auto converted = toEngine(path);
    if (!converted.startsWith('/') || converted.startsWith("//")) return argument;
    // Relative source arguments keep saved-PDF SyncTeX paths usable on Windows.
    return prefix + relativePath(workingDirectory, converted);
}

QString TexpressoWsl::windowsText(const QByteArray &bytes)
{
    if (!bytes.contains('\0')) return QString::fromUtf8(bytes).trimmed();
    QString result;
    for (int i = 0; i + 1 < bytes.size(); i += 2) {
        const auto code = uchar(bytes[i]) | (quint16(uchar(bytes[i + 1])) << 8);
        if (code && code != 0xfeff) result += QChar(code);
    }
    return result.trimmed();
}
int TexpressoWsl::distributionVersion(const QString &listing, const QString &distribution)
{
    const QRegularExpression row("^\\s*\\*?\\s*" + QRegularExpression::escape(distribution) + "\\s+.+\\s+([12])\\s*$");
    for (const auto &line : listing.split('\n')) {
        const auto match = row.match(line);
        if (match.hasMatch()) return match.captured(1).toInt();
    }
    return -1;
}
