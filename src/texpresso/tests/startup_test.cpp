#include <QtTest>
#include "texpressomanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <csignal>
#include <cerrno>

namespace {
QString literal(const QString &text)
{
    const auto array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
struct ControlledWorker {
    QTemporaryDir directory;
    QString frontend, root, marker, release;
    bool create(bool timeout = false)
    {
        const auto engine = qEnvironmentVariable("TEXPRESSO_TEST_ENGINE");
        if (!directory.isValid() || !QDir(directory.path()).mkdir("bin")) return false;
        frontend = directory.filePath("bin/texpresso"); root = directory.filePath("main.tex");
        marker = directory.filePath("starting"); release = directory.filePath("release");
        if (!QFile::copy(engine, frontend)) return false;
        const auto runtime = QFileInfo(engine).dir();
        const auto libraries = runtime.absoluteFilePath("../lib");
        if (QFileInfo::exists(libraries) && !QFile::link(libraries, directory.filePath("lib"))) return false;
        const auto realWorker = runtime.absoluteFilePath("texpresso-xetex");
        const auto script = QString(R"PY(#!/usr/bin/env python3
import os, signal, socket, sys, time
marker, release, worker = %1, %2, %3
first = not os.path.exists(marker)
if first:
    with open(marker, 'w') as file: file.write(str(os.getpid()))
    if %4:
        connection = socket.socket(fileno=int(os.environ['TEXPRESSO_FD']))
        greeting = b''
        while len(greeting) < 12: greeting += connection.recv(12 - len(greeting))
        assert greeting == b'TEXPRESSOS03'
        connection.sendall(b'TEXP')
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        time.sleep(60)
    else:
        while not os.path.exists(release): time.sleep(0.005)
os.execv(worker, [worker] + sys.argv[1:])
)PY").arg(literal(marker), literal(release), literal(realWorker), timeout ? "True" : "False");
        const auto shim = directory.filePath("bin/texpresso-xetex");
        if (!writeFile(shim, script.toUtf8()) || !QFile::setPermissions(shim, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return false;
        return writeFile(root, "\\documentclass{article}\n\\begin{document}\nDisk source.\\end{document}\n");
    }
    qint64 workerPid() const
    {
        QFile file(marker); if (!file.open(QIODevice::ReadOnly)) return 0;
        return file.readAll().toLongLong();
    }
};
struct StartupDeadline {
    QByteArray previous = qgetenv("TEXPRESSO_TEST_STARTUP_TIMEOUT_MS");
    StartupDeadline() { qputenv("TEXPRESSO_TEST_STARTUP_TIMEOUT_MS", "1000"); }
    ~StartupDeadline() { restore(); }
    void restore()
    {
        if (previous.isNull()) qunsetenv("TEXPRESSO_TEST_STARTUP_TIMEOUT_MS");
        else qputenv("TEXPRESSO_TEST_STARTUP_TIMEOUT_MS", previous);
    }
};
}

class StartupTest : public QObject
{
    Q_OBJECT
private slots:
    void editsAndPauseBeforeGreeting();
    void stopBeforeGreeting();
    void failedGreetingAndRestart();
};

void StartupTest::editsAndPauseBeforeGreeting()
{
    if (qEnvironmentVariableIsEmpty("TEXPRESSO_TEST_ENGINE")) QSKIP("Set TEXPRESSO_TEST_ENGINE for controlled startup integration");
    ControlledWorker worker; QVERIFY(worker.create());
    TexpressoManager manager(nullptr, worker.frontend); manager.activate(worker.root);
    auto live = manager.current(); live->followCursor = false; live->setAutoRerun(false);
    QTRY_VERIFY_WITH_TIMEOUT(worker.workerPid() > 0, 15000);
    const auto pid = live->processId();
    live->setPaused(true);
    manager.openBuffer(worker.root, "\\documentclass{article}\\begin{document}Intermediate.\\end{document}\n");
    manager.reconcileBuffer(worker.root, "\\documentclass{article}\\begin{document}Latest.\\typeout{STARTUP-LATEST}\\end{document}\n");
    // The matching source barrier is acknowledged while the worker is gated.
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.revision() >= 0, 5000);
    QVERIFY(live->userPaused); QVERIFY(!live->preview.valid());
    QVERIFY(writeFile(worker.release, {}));
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("handshake success"), 15000);
    QTest::qWait(100);
    QVERIFY(!live->compilerOutput.contains("STARTUP-LATEST"));
    live->setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() && live->compilerOutput.contains("STARTUP-LATEST"), 20000);
    QCOMPARE(live->processId(), pid);
}

void StartupTest::stopBeforeGreeting()
{
    if (qEnvironmentVariableIsEmpty("TEXPRESSO_TEST_ENGINE")) QSKIP("Set TEXPRESSO_TEST_ENGINE for controlled startup integration");
    ControlledWorker worker; QVERIFY(worker.create());
    TexpressoManager manager(nullptr, worker.frontend); manager.activate(worker.root);
    auto live = manager.current();
    QTRY_VERIFY_WITH_TIMEOUT(worker.workerPid() > 0, 15000);
    const auto pid = worker.workerPid(); live->stop();
    QTRY_COMPARE_WITH_TIMEOUT(live->processId(), qint64(0), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(::kill(pid, 0) == -1 && errno == ESRCH, 5000);
    QVERIFY(live->userStopped);
}

void StartupTest::failedGreetingAndRestart()
{
    if (qEnvironmentVariableIsEmpty("TEXPRESSO_TEST_ENGINE")) QSKIP("Set TEXPRESSO_TEST_ENGINE for controlled startup integration");
    ControlledWorker worker; QVERIFY(worker.create(true)); StartupDeadline deadline;
    TexpressoManager manager(nullptr, worker.frontend); manager.activate(worker.root);
    auto live = manager.current(); live->followCursor = false; live->setAutoRerun(false);
    QTRY_VERIFY_WITH_TIMEOUT(worker.workerPid() > 0, 15000);
    const auto pid = worker.workerPid();
    QTRY_VERIFY_WITH_TIMEOUT(live->engineOutput.contains("startup timed out") && !live->previewError.isEmpty(), 10000);
    QVERIFY(!live->preview.valid());
    QTRY_VERIFY_WITH_TIMEOUT(::kill(pid, 0) == -1 && errno == ESRCH, 5000);
    manager.openBuffer(worker.root, "\\documentclass{article}\\begin{document}Recovered.\\typeout{STARTUP-RECOVERED}\\end{document}\n");
    deadline.restore(); manager.restart(); live = manager.current();
    QTRY_VERIFY_WITH_TIMEOUT(live->preview.valid() && live->compilerOutput.contains("STARTUP-RECOVERED"), 20000);
}

QTEST_MAIN(StartupTest)
#include "startup_test.moc"
