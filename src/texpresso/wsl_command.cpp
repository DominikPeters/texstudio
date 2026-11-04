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
    parser.addOption({"provider", "TeX package provider (auto, texlive, miktex)", "provider", "auto"});
    parser.addOption({"distribution", "WSL distribution (default if omitted)", "name"});
    parser.addPositionalArgument("command", "Linux command and arguments", "-- COMMAND [ARGUMENTS...]");
    parser.process(app);
    auto commands = parser.positionalArguments();
    if (commands.isEmpty()) { parser.showHelp(2); }
    TexpressoWsl wsl; wsl.enabled = true; wsl.distribution = parser.value("distribution");
    wsl.requestedProvider = parser.value("provider");
    wsl.prepare(QDir::currentPath());
    if (!wsl.error.isEmpty()) { std::fprintf(stderr, "%s\n", qPrintable(wsl.error)); return 1; }
    const auto working = wsl.toEngine(QDir::currentPath());
    for (int i = 1; i < commands.size(); ++i)
        commands[i] = wsl.commandArgument(commands[i], working);
    const auto supervisor = wsl.toEngine(QCoreApplication::applicationDirPath() + "/texpresso-runtime/bin/texpresso-wsl");
    QStringList arguments = {"-c", TexpressoWsl::environmentScript(wsl.requestedProvider), "texstudio-build", supervisor,
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
