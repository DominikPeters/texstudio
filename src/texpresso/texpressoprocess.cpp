#include "texpressoprocess.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <csignal>
#include <unistd.h>

TexpressoChild::TexpressoChild(QObject *parent) : QProcess(parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    setChildProcessModifier([] { if (::setsid() < 0) ::_exit(127); });
#endif
}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
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
    env.insert("PATH", paths.join(QDir::listSeparator()));
    return env;
}

TexpressoProcess::TexpressoProcess(QObject *parent) : QObject(parent)
{
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
            emit failed(tr("The bundled TeXpresso engine does not support PNG preview protocol 1.")); return;
        }
        child.setProgram(program);
        child.setArguments({"-texlive", "-headless", "-json", "-lines", rootFile});
        child.setWorkingDirectory(QFileInfo(rootFile).absolutePath());
        child.setProcessEnvironment(texliveEnvironment());
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
    connect(&protocol, &TexpressoProtocol::message, this, &TexpressoProcess::message);
    connect(&protocol, &TexpressoProtocol::image, this, &TexpressoProcess::image);
    connect(&protocol, &TexpressoProtocol::failed, this, [this](const QString &reason) {
        if (!stopping) { emit failed(reason); stop(); }
    });
}

TexpressoProcess::~TexpressoProcess()
{
    stopping = true;
    probe.kill(); probe.waitForFinished(1000);
    killGroup(); child.kill(); child.waitForFinished(1000);
}
void TexpressoProcess::start(const QString &engine, const QString &root)
{
    program = engine; rootFile = root;
    const auto env = texliveEnvironment();
    if (!QFileInfo(engine).isExecutable()) { emit failed(tr("The bundled TeXpresso engine is missing. Reinstall this application.")); return; }
    if (QStandardPaths::findExecutable("kpsewhich", env.value("PATH").split(QDir::listSeparator())).isEmpty()) {
        emit failed(tr("TeX Live was not found. Install TeX Live, then restart the preview.")); return;
    }
    probe.setProcessEnvironment(env);
    probe.start(engine, {"-capabilities"}); probeTimeout.start(5000);
}
void TexpressoProcess::send(const QJsonArray &command)
{
    if (!stopping && child.state() == QProcess::Running)
        child.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
}
void TexpressoProcess::stop()
{
    stopping = true; probeTimeout.stop(); probe.kill();
    child.closeWriteChannel();
    if (processGroup > 0) ::kill(-processGroup, SIGTERM);
    shutdownTimer.start(750);
}
void TexpressoProcess::killGroup()
{
    shutdownTimer.stop();
    if (processGroup > 0) { ::kill(-processGroup, SIGKILL); processGroup = 0; }
}
