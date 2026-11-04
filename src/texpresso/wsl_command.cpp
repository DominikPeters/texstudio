// Native Windows command adapter for TeXstudio's existing build recipes.
// GPL-3.0-only. Compiler IDs, reruns, logs and bibliography handling remain in
// BuildManager; only execution and path arguments move into the selected WSL.
#include "texpressowsl.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <cstdio>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"distribution", "WSL distribution (default if omitted)", "name"});
    parser.addPositionalArgument("command", "Linux command and arguments", "-- COMMAND [ARGUMENTS...]");
    parser.process(app);
    auto commands = parser.positionalArguments();
    if (commands.isEmpty()) { parser.showHelp(2); }
    TexpressoWsl wsl; wsl.enabled = true; wsl.distribution = parser.value("distribution");
    wsl.prepare(QDir::currentPath());
    if (!wsl.error.isEmpty()) { std::fprintf(stderr, "%s\n", qPrintable(wsl.error)); return 1; }
    const auto working = wsl.toEngine(QDir::currentPath());
    for (int i = 1; i < commands.size(); ++i) {
        const auto value = commands[i];
        QString prefix, path = value;
        const int equals = value.indexOf('=');
        if (equals >= 0) { prefix = value.left(equals + 1); path = value.mid(equals + 1); }
        // Arguments can contain TeX expressions: only absolute Windows paths
        // are path-converted, never arbitrary backslashes or source text.
        QString normalized = path; normalized.replace('\\', '/');
        if (!(normalized.size() > 2 && normalized[1] == ':' && normalized[2] == '/')
            && !normalized.startsWith("//wsl.localhost/", Qt::CaseInsensitive)
            && !normalized.startsWith("//wsl$/", Qt::CaseInsensitive)) continue;
        const auto converted = wsl.toEngine(path);
        if (converted != path) {
            // Relative source arguments keep saved-PDF SyncTeX project paths
            // usable by the Windows viewer as well as the live viewer.
            commands[i] = prefix + TexpressoWsl::relativePath(working, converted);
        }
    }
    const auto supervisor = wsl.toEngine(QCoreApplication::applicationDirPath() + "/texpresso-runtime/bin/texpresso-wsl");
    QStringList arguments = {"-c", TexpressoWsl::environmentScript(), "texstudio-build", supervisor,
                             working, "--", "/usr/bin/env"};
    arguments << commands;
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    // Own the bridge's input pipe. Stopping this Windows process closes the
    // pipe even if the editor did not provide stdin for the compiler recipe.
    process.start("wsl.exe", wsl.arguments("sh", arguments));
    if (!process.waitForStarted(10000) || !process.waitForFinished(-1)) {
        std::fprintf(stderr, "%s\n", qPrintable(process.errorString())); return 1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}
