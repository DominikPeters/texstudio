#include "texpressoprovider.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>

QString TexpressoProvider::resolve(const QString &requested, const QProcessEnvironment &environment)
{
    const auto paths = environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    QString texlive;
    for (const auto &path : paths) {
        const auto candidate = QStandardPaths::findExecutable("kpsewhich", {path});
        const auto compatibility = QStandardPaths::findExecutable("findtexmf", {path});
        if (!candidate.isEmpty() && compatibility.isEmpty()) { texlive = candidate; break; }
    }
    const auto miktex = QStandardPaths::findExecutable("findtexmf", paths);
    if (requested == "miktex") return miktex.isEmpty() ? QString() : requested;
    if (requested == "texlive") return texlive.isEmpty() ? QString() : requested;
    if (requested != "auto" && !requested.isEmpty()) return {};
    // MiKTeX also ships a compatibility kpsewhich. Do not mistake that for
    // TeX Live when both commands resolve into the same installation.
    if (texlive.isEmpty() && !miktex.isEmpty()) return "miktex";
    return texlive.isEmpty() ? QString() : QStringLiteral("texlive");
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
