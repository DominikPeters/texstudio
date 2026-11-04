#include "texpressoprocess.h"
#include "texpressoprovider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>
#include <csignal>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#include <QtConcurrent>
namespace { QString configuredWslDistribution, configuredProvider = "auto"; bool configuredInstallPackages = false; }
void TexpressoProcess::setPackageProvider(const QString &provider, bool installPackages)
{ configuredProvider = provider; configuredInstallPackages = installPackages; }
void TexpressoProcess::setWslDistribution(const QString &distribution) { configuredWslDistribution = distribution; }
#include <algorithm>

TexpressoChild::TexpressoChild(QObject *parent) : QProcess(parent)
{
#ifdef Q_OS_WIN
    setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && !defined(Q_OS_WIN)
    setChildProcessModifier([] { if (::setsid() < 0) ::_exit(127); });
#endif
}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0) && !defined(Q_OS_WIN)
void TexpressoChild::setupChildProcess() { if (::setsid() < 0) ::_exit(127); }
#endif

QString TexpressoProcess::bundledEngine()
{
    const QDir application(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    return application.absoluteFilePath("../Helpers/texpresso/bin/texpresso");
#else
    const auto staged = application.absoluteFilePath("texpresso-runtime/bin/texpresso");
    if (QFileInfo::exists(staged)) return staged;
    return application.absoluteFilePath(TEXPRESSO_INSTALL_RELATIVE "/bin/texpresso");
#endif
}

QProcessEnvironment TexpressoProcess::texliveEnvironment()
{
    auto env = QProcessEnvironment::systemEnvironment();
    auto paths = env.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    // Finder-launched applications do not inherit shell configuration.
#ifdef Q_OS_MACOS
    if (QFileInfo::exists("/Library/TeX/texbin/kpsewhich")) paths.prepend("/Library/TeX/texbin");
#endif
    const QDir installations("/usr/local/texlive");
    const auto years = installations.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const auto &year : years) {
        const QDir bin(installations.absoluteFilePath(year + "/bin"));
        for (const auto &arch : bin.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto directory = bin.absoluteFilePath(arch);
            if (QFileInfo::exists(directory + "/kpsewhich") && !paths.contains(directory)) paths.append(directory);
        }
    }
    const QStringList miktexPaths = {QDir::homePath() + "/bin", QDir::homePath() + "/.local/bin",
        QDir::homePath() + "/.miktex/texmfs/install/miktex/bin", "/opt/miktex/bin", "/opt/homebrew/bin", "/usr/local/bin"};
    for (const auto &path : miktexPaths)
        if (QFileInfo::exists(path + "/findtexmf") && !paths.contains(path)) paths.append(path);
    env.insert("PATH", paths.join(QDir::listSeparator()));
    return env;
}

TexpressoProcess::TexpressoProcess(QObject *parent) : QObject(parent)
{
    traceProtocol = qEnvironmentVariableIsSet("TEXPRESSO_TEST_TRACE_PROTOCOL");
#ifdef Q_OS_WIN
    probe.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    probeTimeout.setSingleShot(true); shutdownTimer.setSingleShot(true);
    connect(&probeTimeout, &QTimer::timeout, this, [this] {
        probe.kill(); emit failed(tr("The bundled TeXpresso capability check timed out."));
    });
    connect(&shutdownTimer, &QTimer::timeout, this, &TexpressoProcess::killGroup);
    connect(&probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!stopping && error == QProcess::FailedToStart) {
            probeTimeout.stop(); emit failed(tr("Cannot launch bundled TeXpresso: %1").arg(probe.errorString()));
        }
    });
    connect(&probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        probeTimeout.stop();
        if (stopping) return;
        const auto features = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        if (status != QProcess::NormalExit || code || features["previewProtocol"].toInt() != 1
            || !features["codecs"].toArray().contains("png")) {
            emit failed(tr("The bundled TeXpresso engine does not support PNG preview protocol 1. %1").arg(QString::fromUtf8(probe.readAllStandardError()).trimmed())); return;
        }
        child.setProgram(wsl.enabled ? QStringLiteral("wsl.exe") : program);
        QStringList arguments = TexpressoProvider::engineArguments(provider, installPackages, features);
        if (arguments.isEmpty()) {
            emit failed(tr("The bundled engine does not support the selected TeX package provider or its installation option. Update the application or choose TeX Live in TeX Environment.")); return;
        }
        arguments << "-headless" << "-json" << "-lines";
        if (features["parentWatch"].toBool()) arguments << "-watch-parent";
        if (features["maxPreviewPixels"].toInt() > 4000000)
            arguments << "-preview-max-pixels" << QString::number(std::min(features["maxPreviewPixels"].toInt(), previewPixelLimit));
        for (const auto &directory : inclusionDirectories) arguments << "-I" << wsl.toEngine(directory);
        arguments << wsl.toEngine(rootFile);
        if (wsl.enabled) {
            const auto engine = wsl.toEngine(program);
            const auto supervisor = engine.left(engine.lastIndexOf('/') + 1) + "texpresso-wsl";
            QStringList command = {"-c", TexpressoWsl::environmentScript(provider), "texstudio-wsl", supervisor,
                                   wsl.toEngine(QFileInfo(rootFile).absolutePath()), "--", engine};
            command << arguments;
            child.setArguments(wsl.arguments("sh", command));
        } else {
            child.setArguments(arguments);
            child.setWorkingDirectory(QFileInfo(rootFile).absolutePath());
        }
        child.setProcessEnvironment(wsl.enabled ? texliveEnvironment() : probe.processEnvironment());
        connect(&child, &QProcess::started, this, [this, features] {
            processGroup = child.processId();
            if (stopping) stop(); else emit ready(features);
        });
        child.start();
    });
    connect(&child, &QProcess::readyReadStandardOutput, this, [this] { protocol.feed(child.readAllStandardOutput()); });
    connect(&child, &QProcess::readyReadStandardError, this, [this] {
        const auto text = QString::fromUtf8(child.readAllStandardError());
        emit diagnostic(text);
    });
    connect(&child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!stopping && error == QProcess::FailedToStart) emit failed(child.errorString());
    });
    connect(&child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        protocol.feed(child.readAllStandardOutput());
        if (!stopping) {
            protocol.finish();
            emit failed(tr("TeXpresso stopped (%1).").arg(status == QProcess::CrashExit ? tr("crashed") : QString::number(code)));
        }
        // Reap worker/checkpoint processes even when the frontend has exited.
        killGroup(); emit stopped();
    });
    connect(&protocol, &TexpressoProtocol::message, this, [this](const QJsonArray &message) {
        const auto verb = message.isEmpty() ? QString() : message[0].toString();
        if (traceProtocol && (verb.startsWith("preview-") || verb == "reset-sync" || verb == "truncate-lines" || verb == "engine-error")) {
            const auto line = "[protocol-receive] " + QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
            qInfo().noquote() << line; emit diagnostic(line + '\n');
        }
        emit this->message(wsl.incoming(message));
    });
    connect(&initialization, &QFutureWatcher<TexpressoWsl>::finished, this, [this] {
        if (stopping) return;
        wsl = initialization.result();
        provider = wsl.provider;
        if (!wsl.error.isEmpty()) { emit failed(wsl.error); return; }
        startProbe();
    });
    connect(&protocol, &TexpressoProtocol::image, this, [this](QJsonObject header, const QByteArray &bytes) {
        if (wsl.enabled && header["annotations"].isObject()) {
            auto annotations = header["annotations"].toObject();
            if (annotations.contains("figures")) {
                auto figures = annotations["figures"].toArray();
                for (int i = 0; i < figures.size(); ++i) {
                    auto figure = figures[i].toObject();
                    figure["file"] = wsl.toEditor(figure["file"].toString()); figures[i] = figure;
                }
                annotations["figures"] = figures; header["annotations"] = annotations;
            }
        }
        emit image(header, bytes);
    });
    connect(&protocol, &TexpressoProtocol::failed, this, [this](const QString &reason) {
        if (!stopping) { emit failed(reason); stop(); }
    });
}

TexpressoProcess::~TexpressoProcess()
{
    stopping = true;
    probe.kill(); probe.waitForFinished(1000);
    if (wsl.enabled) { child.closeWriteChannel(); child.waitForFinished(1000); }
    killGroup(); child.kill(); child.waitForFinished(1000);
}
void TexpressoProcess::start(const QString &engine, const QString &root, const QStringList &directories, int pixelLimit)
{
    previewPixelLimit = std::clamp(pixelLimit, 1, int(TexpressoProtocol::maxPixels));
    program = engine; rootFile = root; inclusionDirectories = directories;
    const auto requested = qEnvironmentVariable("TEXPRESSO_PACKAGE_PROVIDER", configuredProvider);
    installPackages = configuredInstallPackages;
    wsl.requestedProvider = requested;
#ifdef Q_OS_WIN
    wsl.enabled = true;
    wsl.distribution = qEnvironmentVariable("TEXPRESSO_WSL_DISTRIBUTION", configuredWslDistribution);
    initialization.setFuture(QtConcurrent::run([copy = wsl, root]() mutable { copy.prepare(root); return copy; }));
    return;
#endif
    const auto env = texliveEnvironment();
    if (!QFileInfo(engine).isExecutable()) { emit failed(tr("The bundled TeXpresso engine is missing. Reinstall this application.")); return; }
    provider = TexpressoProvider::resolve(requested, env);
    if (provider.isEmpty()) {
        emit failed(tr("The selected TeX package provider was not found. Install TeX Live or MiKTeX, then select it in TeX Environment.")); return;
    }
    probe.setProcessEnvironment(TexpressoProvider::executionEnvironment(provider, env));
    // A newly installed signed executable may spend seconds in OS cold loading.
    startProbe();
}
void TexpressoProcess::startProbe()
{
    if (wsl.enabled) {
        probe.start("wsl.exe", wsl.arguments("sh", {"-c", TexpressoWsl::environmentScript(provider), "texstudio-wsl", wsl.toEngine(program), "-capabilities"}));
    } else probe.start(program, {"-capabilities"});
    probeTimeout.start(wsl.enabled ? 30000 : 10000);
}
void TexpressoProcess::send(const QJsonArray &command)
{
    if (!stopping && child.state() == QProcess::Running) {
        const auto bytes = QJsonDocument(wsl.outgoing(command)).toJson(QJsonDocument::Compact);
        if (traceProtocol) {
            const auto line = "[protocol-send] " + QString::fromUtf8(bytes).left(4096);
            qInfo().noquote() << line; emit diagnostic(line + '\n');
        }
        child.write(bytes + '\n');
    }
}
void TexpressoProcess::stop()
{
    stopping = true; probeTimeout.stop(); probe.kill();
    child.closeWriteChannel();
#ifndef Q_OS_WIN
    if (processGroup > 0) ::kill(-processGroup, SIGTERM);
#endif
    shutdownTimer.start(750);
}
void TexpressoProcess::killGroup()
{
    shutdownTimer.stop();
#ifndef Q_OS_WIN
    if (processGroup > 0) ::kill(-processGroup, SIGKILL);
#else
    if (child.state() != QProcess::NotRunning) child.kill();
#endif
    processGroup = 0;
}
