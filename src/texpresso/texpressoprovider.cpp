#include "texpressoprovider.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>

namespace {
QString providerExecutable(const QString &provider, const QProcessEnvironment &environment)
{
    const auto paths = environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    if (provider == "miktex") return QStandardPaths::findExecutable("findtexmf", paths);
    for (const auto &path : paths) {
        const auto candidate = QStandardPaths::findExecutable("kpsewhich", {path});
        const auto compatibility = QStandardPaths::findExecutable("findtexmf", {path});
        if (!candidate.isEmpty() && compatibility.isEmpty()) return candidate;
    }
    return {};
}
}
QString TexpressoProvider::resolve(const QString &requested, const QProcessEnvironment &environment)
{
    if (requested == "miktex" || requested == "texlive") return providerExecutable(requested, environment).isEmpty() ? QString() : requested;
    if (requested != "auto" && !requested.isEmpty()) return {};
    // MiKTeX's compatibility kpsewhich does not indicate a TeX Live install.
    if (!providerExecutable("texlive", environment).isEmpty()) return "texlive";
    return providerExecutable("miktex", environment).isEmpty() ? QString() : QStringLiteral("miktex");
}
QProcessEnvironment TexpressoProvider::executionEnvironment(const QString &requested, QProcessEnvironment environment)
{
    const auto selected = resolve(requested, environment);
    if (selected.isEmpty()) return environment;
    auto paths = environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    const auto path = QFileInfo(providerExecutable(selected, environment)).absolutePath();
    paths.removeAll(path); paths.prepend(path);
    environment.insert("PATH", paths.join(QDir::listSeparator()));
    return environment;
}
QString TexpressoProvider::shellSetup(const QString &requested)
{
    // Only enum values become shell literals. Paths from users remain argv.
    const auto selection = requested == "texlive" || requested == "miktex" ? requested : QStringLiteral("auto");
    return QStringLiteral(
        "for p in /usr/local/texlive/*/bin/* \"$HOME\"/.local/share/texlive/*/bin/* "
        "\"$HOME\"/bin \"$HOME\"/.local/bin \"$HOME\"/.miktex/texmfs/install/miktex/bin "
        "\"$HOME\"/.miktex/texmfs/install/miktex/bin/* /opt/miktex/bin; do "
        "if [ -x \"$p/kpsewhich\" ] || [ -x \"$p/findtexmf\" ]; then PATH=\"$PATH:$p\"; fi; done; export PATH; "
        "TXP_PACKAGE_PROVIDER=''; TXP_TL_DIR=''; TXP_MI_DIR=''; txp_rest=\"$PATH:\"; "
        "while [ -n \"$txp_rest\" ]; do p=\"${txp_rest%%:*}\"; txp_rest=\"${txp_rest#*:}\"; [ -n \"$p\" ] || p=.; "
        "if [ -x \"$p/kpsewhich\" ] && [ ! -x \"$p/findtexmf\" ] && [ -z \"$TXP_TL_DIR\" ]; then TXP_TL_DIR=\"$p\"; fi; "
        "if [ -x \"$p/findtexmf\" ] && [ -z \"$TXP_MI_DIR\" ]; then TXP_MI_DIR=\"$p\"; fi; done; "
        "case '%1' in "
        "texlive) [ -z \"$TXP_TL_DIR\" ] || TXP_PACKAGE_PROVIDER=texlive ;; "
        "miktex) [ -z \"$TXP_MI_DIR\" ] || TXP_PACKAGE_PROVIDER=miktex ;; "
        "auto) if [ -n \"$TXP_TL_DIR\" ]; then TXP_PACKAGE_PROVIDER=texlive; "
        "elif [ -n \"$TXP_MI_DIR\" ]; then TXP_PACKAGE_PROVIDER=miktex; fi ;; esac; "
        "case \"$TXP_PACKAGE_PROVIDER\" in texlive) PATH=\"$TXP_TL_DIR:$PATH\" ;; miktex) PATH=\"$TXP_MI_DIR:$PATH\" ;; esac; "
        "export PATH TXP_PACKAGE_PROVIDER; ").arg(selection);
}
QStringList TexpressoProvider::engineArguments(const QString &provider, bool installPackages, const QJsonObject &capabilities)
{
    if (provider == "texlive") return {"-texlive"}; // Legacy engines support it too.
    if (provider != "miktex" || !capabilities.value("packageProviders").toArray().contains("miktex")) return {};
    QStringList result = {"-miktex"};
    if (installPackages) {
        if (!capabilities.value("miktexAutoInstall").toBool()) return {};
        result << "-miktex-auto-install";
    }
    return result;
}
